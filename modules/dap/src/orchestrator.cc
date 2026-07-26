#include "dap/orchestrator.hpp"

#include <cassert>
#include <iostream>
#include <utility>
#include <variant>

#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/ErrorHandling.h"

namespace dap {

Orchestrator::Orchestrator(Transport transport) : transport_(std::move(transport)) {}

void Orchestrator::RegisterHandler(std::string command, IRequestHandler &handler) {
    const auto [it, inserted] = command_handlers_.emplace(std::move(command), &handler);
    if (!inserted) {
        std::cerr << "dap: command '" << it->first << "' registered by more than one handler\n";
    }
}

IRequestHandler::FeatureSet Orchestrator::AggregatedHandlerFeatures() const {
    IRequestHandler::FeatureSet features;
    for (const auto &kv : command_handlers_) {
        IRequestHandler::FeatureSet handler_features = kv.second->GetSupportedFeatures();
        features.insert(handler_features.begin(), handler_features.end());
    }
    return features;
}

void Orchestrator::Run() {
    while (!done_) {
        std::optional<llvm::json::Value> message = transport_.ReadMessage();
        if (!message) {
            break;
        }

        protocol::Request request;
        llvm::json::Path::Root root{"message"};
        if (!fromJSON(*message, request, root)) {
            std::cerr << "dap: received a message that isn't a well-formed request: "
                      << llvm::toString(root.getError()) << "\n";
            continue;
        }

        HandleRequest(request);
    }
}

void Orchestrator::HandleRequest(const protocol::Request &request) {
    active_request_ = &request;
    const llvm::scope_exit cleanup([&] {
        active_request_ = nullptr;
    });

    if (const auto handler_it = command_handlers_.find(request.command); handler_it != command_handlers_.end()) {
        handler_it->second->Run(request);
    }else {
        SendErrorResponse(request, "unrecognized request: '" + request.command + "'");
    }
}

protocol::Id Orchestrator::Send(protocol::Message message) {
    std::lock_guard<std::mutex> guard(send_mutex_);
    protocol::Message msg = std::visit(
        [this](auto &&m) -> protocol::Message {
            if (m.seq == protocol::kCalculateSeq) {
                m.seq = NextSeq();
            }
            assert(m.seq > 0 && "message sequence must be greater than zero.");
            return m;
        },
        std::move(message));

    if (const auto *event = std::get_if<protocol::Event>(&msg)) {
        SendMessage(toJSON(*event));
        return event->seq;
    }
    if (const auto *req = std::get_if<protocol::Request>(&msg)) {
        SendMessage(toJSON(*req));
        return req->seq;
    }
    if (const auto *resp = std::get_if<protocol::Response>(&msg)) {
        SendMessage(toJSON(*resp));
        return resp->seq;
    }

    llvm_unreachable("Unexpected message type");
}

bool Orchestrator::IsCancelled(const protocol::Request &request) const {
    std::lock_guard<std::mutex> guard(cancelled_requests_mutex_);
    return cancelled_requests_.contains(request.seq);
}

void Orchestrator::ClearCancelRequest(const protocol::CancelArguments &args) {
    std::lock_guard<std::mutex> guard(cancelled_requests_mutex_);
    if (args.requestId) {
        cancelled_requests_.erase(*args.requestId);
    }
}

void Orchestrator::SendErrorResponse(const protocol::Request &request, std::string message) {
    protocol::Response response;
    response.command = request.command;
    response.request_seq = request.seq;
    response.seq = NextSeq();
    response.success = false;
    response.message = std::move(message);
    SendMessage(toJSON(response));
}

bool Orchestrator::SendMessage(const llvm::json::Value &message) {
    if (!transport_.WriteMessage(message)) {
        std::cerr << "dap: failed to write message\n";
        return false;
    }
    return true;
}

void Orchestrator::SendEvent(llvm::StringRef event, std::optional<llvm::json::Value> body) {
    protocol::Event evt;
    evt.event = event.str();
    evt.body = std::move(body);
    evt.seq = NextSeq();
    SendMessage(toJSON(evt));
}

}  // namespace dap
