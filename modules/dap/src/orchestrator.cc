#include "dap/orchestrator.hpp"

#include <iostream>
#include <utility>

namespace dap {

Orchestrator::Orchestrator(Transport transport) : transport_(std::move(transport)) {}

void Orchestrator::RegisterService(Service &service) {
    for (const std::string &command : service.SupportedCommands()) {
        const auto [it, inserted] = command_owners_.emplace(command, &service);
        if (!inserted) {
            std::cerr << "dap: command '" << command << "' registered by more than one service\n";
        }
    }
}

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
            break;  // EOF or a malformed frame: nothing more to usefully read.
        }

        protocol::Request request;
        llvm::json::Path::Root root("message");
        if (!fromJSON(*message, request, root)) {
            std::cerr << "dap: received a message that isn't a well-formed request: "
                      << llvm::toString(root.getError()) << "\n";
            continue;
        }

        HandleRequest(request);
    }
}

void Orchestrator::HandleRequest(const protocol::Request &request) {
    // Handlers are checked first: a handler owns exactly one command and is
    // the preferred routing seam (see dap::IRequestHandler); Service is the
    // coarser-grained fallback for any future service that dispatches its
    // own commands internally instead of registering per-command handlers.
    if (const auto handler_it = command_handlers_.find(request.command); handler_it != command_handlers_.end()) {
        handler_it->second->Run(request);
        return;
    }

    const auto it = command_owners_.find(request.command);
    if (it == command_owners_.end()) {
        SendErrorResponse(request, "unrecognized request: '" + request.command + "'");
        return;
    }
    // The owning service is responsible for sending its own response (and
    // any events) via this Orchestrator - see dap::Service.
    it->second->HandleRequest(request);
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
