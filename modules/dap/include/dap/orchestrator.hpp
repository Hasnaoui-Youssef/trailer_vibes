#ifndef TRAILER_DAP_ORCHESTRATOR_HPP_
#define TRAILER_DAP_ORCHESTRATOR_HPP_

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

    // Starts a reader thread (reads, decodes, enqueues) and runs the
    // dispatch loop (pops the queue, handles one request at a time) on the
    // calling thread until both are done. Dispatch stays single-threaded -
    // this only decouples reading the next message from handling the
    // current one, so a queued 'cancel' can be observed while a long
    // request is still in flight.
    void Run();

    // Locks send_mutex_ before writing. Every outbound message - response,
    // event or reverse request - must go through this (or Send()) so writes
    // to the wire never interleave across threads.
    bool SendMessage(const llvm::json::Value &message);

    //Convenience wrapper over Send() for events
    void SendEvent(llvm::StringRef event, std::optional<llvm::json::Value> body = std::nullopt);

    // Interrupts the reader thread's blocking read - the dispatch loop then
    // winds down on its own once the queue drains.
    void RequestStop() { transport_.RequestStop(); }

    //Assigns seq id and then calls SendMessage
    protocol::Id Send(protocol::Message message);

    // Populated by the reader thread when it sees a 'cancel' request,
    // before that request is even enqueued for dispatch - this is what
    // lets a queued or in-flight request be observed as cancelled.
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
    // Reads, decodes and enqueues messages until the transport gives up
    // (real EOF or RequestStop()). Intercepts 'cancel' to populate
    // cancelled_requests_ before the request is enqueued, so a request
    // still sitting in the queue - or already dispatched - can be observed
    // as cancelled.
    void ReaderLoop();

    // Pops one request at a time and calls HandleRequest() on it, in the
    // order the reader enqueued them. Exits once the reader is done and the
    // queue is empty.
    void DispatchLoop();

    void HandleRequest(const protocol::Request &request);
    void SendErrorResponse(const protocol::Request &request, std::string message);

    //DO NOT INCREMENT SEQ ANY OTHER WAY. Only called from Send(), under send_mutex_.
    protocol::Id NextSeq() { return next_seq_++; }

    // Writes without taking send_mutex_. Only called from Send(), which already holds it.
    bool SendMessageLocked(const llvm::json::Value &message);

    Transport transport_;
    std::unordered_map<std::string, IRequestHandler *> command_handlers_;
    protocol::Id next_seq_ = 1;

    std::thread reader_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<protocol::Request> request_queue_;
    bool reader_done_ = false;

    std::mutex send_mutex_;

    mutable std::mutex cancelled_requests_mutex_;
    llvm::SmallSet<int64_t, 4> cancelled_requests_;

    llvm::DenseSet<protocol::ClientFeature> client_features_;

    llvm::unique_function<void()> deferred_configuration_response_;

    std::mutex reverse_requests_mutex_;
    llvm::SmallDenseMap<int64_t, std::unique_ptr<ResponseHandler>> inflight_reverse_requests_;
};

}  // namespace dap

#endif  // TRAILER_DAP_ORCHESTRATOR_HPP_
