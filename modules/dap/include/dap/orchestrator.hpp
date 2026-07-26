#ifndef TRAILER_DAP_ORCHESTRATOR_HPP_
#define TRAILER_DAP_ORCHESTRATOR_HPP_

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/request_handler.hpp"
#include "dap/response_handler.hpp"
#include "dap/service.hpp"
#include "dap/transport.hpp"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

namespace dap {

class Orchestrator {
public:
    explicit Orchestrator(Transport transport);

    //Handlers must outlive the orchestrator
    void RegisterHandler(std::string command, IRequestHandler &handler);

    IRequestHandler::FeatureSet AggregatedHandlerFeatures() const;

    void Run();

    //DO NOT INCREMENT SEQ ANY OTHER WAY
    protocol::Id NextSeq() { return next_seq_++; }

    bool SendMessage(const llvm::json::Value &message);

    //Convenience wrapper over SendMessage for events
    void SendEvent(llvm::StringRef event, std::optional<llvm::json::Value> body = std::nullopt);

    void RequestStop() { done_ = true; }

    //Assigns seq id and then calls SendMessage
    protocol::Id Send(protocol::Message message);

    // Cancellation is a no-op currently
    // Run()'s loop is single-threaded and fully synchronous
    // a cancel message can't be observed until the request would have finished
    bool IsCancelled(const protocol::Request &request) const;
    void ClearCancelRequest(const protocol::CancelArguments &args);

    void SetClientFeatures(llvm::DenseSet<protocol::ClientFeature> features) {
        client_features_ = std::move(features);
    }
    bool ClientFeatureEnabled(protocol::ClientFeature feature) const { return client_features_.contains(feature); }

    llvm::StringRef ClientName() const { return "trailer-dap"; }

    void SetDeferredConfigurationResponse(llvm::unique_function<void()> fn) {
        deferred_configuration_response_ = std::move(fn);
    }
    void RunDeferredConfigurationResponse() {
        if (deferred_configuration_response_)
            deferred_configuration_response_();
        deferred_configuration_response_ = nullptr;
    }

    template <typename Handler>
    void SendReverseRequest(llvm::StringRef command, llvm::json::Value arguments) {
        protocol::Id id = Send(protocol::Request{command.str(), std::move(arguments)});
        std::lock_guard<std::mutex> guard(reverse_requests_mutex_);
        inflight_reverse_requests_[id] = std::make_unique<Handler>(command, id);
    }

private:
    void HandleRequest(const protocol::Request &request);
    void SendErrorResponse(const protocol::Request &request, std::string message);

    Transport transport_;
    std::unordered_map<std::string, IRequestHandler *> command_handlers_;
    protocol::Id next_seq_ = 1;
    bool done_ = false;

    std::mutex send_mutex_;

    mutable std::mutex cancelled_requests_mutex_;
    llvm::SmallSet<int64_t, 4> cancelled_requests_;

    llvm::DenseSet<protocol::ClientFeature> client_features_;

    llvm::unique_function<void()> deferred_configuration_response_;

    std::mutex reverse_requests_mutex_;
    llvm::SmallDenseMap<int64_t, std::unique_ptr<ResponseHandler>> inflight_reverse_requests_;

    //May need a lock for multi-session in the future?
    const protocol::Request *active_request_ = nullptr;
};

}  // namespace dap

#endif  // TRAILER_DAP_ORCHESTRATOR_HPP_
