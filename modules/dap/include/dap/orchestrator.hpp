#ifndef TRAILER_DAP_ORCHESTRATOR_HPP_
#define TRAILER_DAP_ORCHESTRATOR_HPP_

#include <optional>
#include <string>
#include <unordered_map>

#include "dap/protocol/protocol_base.hpp"
#include "dap/request_handler.hpp"
#include "dap/service.hpp"
#include "dap/transport.hpp"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

namespace dap {

// Sole owner of the transport (stdin/stdout) and the request dispatch
// table. Runs the read loop, routes each request to whichever registered
// Service owns its command name, and is the only thing that ever calls
// Transport::WriteMessage - services hand results back through this
// object's Send*() methods rather than touching the wire themselves. See
// the plan's "Architecture: multi-service DAP" section.
class Orchestrator {
public:
    explicit Orchestrator(Transport transport);

    // Merges `service`'s SupportedCommands() into the dispatch table.
    // `service` must outlive the Orchestrator. Command names must not
    // collide with an already-registered service.
    void RegisterService(Service &service);

    // Registers `handler` as the owner of `command`. `handler` must outlive
    // the Orchestrator. This is the routing seam for handlers that don't
    // belong to a coarse-grained Service - the Orchestrator dispatches to
    // handlers directly, without knowing (or needing to know) which service,
    // if any, backs them. Checked before the Service table, so a handler and
    // a Service must not claim the same command.
    void RegisterHandler(std::string command, IRequestHandler &handler);

    // Aggregates GetSupportedFeatures() across every handler registered via
    // RegisterHandler - used by services to assemble their `initialize`
    // response Capabilities without owning a handler registry themselves.
    IRequestHandler::FeatureSet AggregatedHandlerFeatures() const;

    // Runs the read-dispatch loop until the transport reports EOF or a
    // service calls RequestStop(). Every request whose command isn't owned
    // by any registered service or handler gets a `success = false` error
    // response rather than being silently dropped, per the DAP spec.
    void Run();

    // The single source of sequence numbers for this session - shared
    // across every service's outgoing responses/events, since DAP's `seq`
    // space is per-connection, not per-service.
    protocol::Id NextSeq() { return next_seq_++; }

    // Writes an already-fully-formed message (response or event). The sole
    // gateway to the transport; every service's Send()/SendJSON()-style
    // method ultimately calls this.
    bool SendMessage(const llvm::json::Value &message);

    // Convenience for a standalone event not tied to any request (e.g. an
    // Orchestrator-level `initialized`, once more than one service exists
    // and capabilities need merging).
    void SendEvent(llvm::StringRef event, std::optional<llvm::json::Value> body = std::nullopt);

    // Signals Run()'s loop to stop after the current request's response has
    // been sent.
    void RequestStop() { done_ = true; }

private:
    void HandleRequest(const protocol::Request &request);
    void SendErrorResponse(const protocol::Request &request, std::string message);

    Transport transport_;
    std::unordered_map<std::string, Service *> command_owners_;
    std::unordered_map<std::string, IRequestHandler *> command_handlers_;
    protocol::Id next_seq_ = 1;
    bool done_ = false;
};

}  // namespace dap

#endif  // TRAILER_DAP_ORCHESTRATOR_HPP_
