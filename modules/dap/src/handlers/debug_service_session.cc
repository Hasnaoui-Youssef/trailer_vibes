//===-- debug_service_session.cc -------------------------------------------===//
//
// Implements dap::CreateDebugServiceSession (declared in the
// public dap/debug_service_factory.hpp). Lives in the handlers target, not
// the debug_service target, deliberately: constructing a session means
// constructing a core::DebugContext, a DebugService *and*
// building+registering handlers, and only the handlers target is allowed
// to depend on all three without creating a cycle (debug_service must not
// depend on handlers or core's forwarding wiring - handlers already depend
// on both debug_service and core).
//
// This is also the composition root for the DebugContext/DebugService
// transitional wiring described in core/debug_context.hpp: DebugContext
// owns the LldbProvider and every already-carved component; DebugService
// (still the owner of everything not yet carved) gets its LldbProvider&
// from DebugContext and holds a DebugContext& of its own for the reverse
// direction (capabilities assembly, stop-reason resolution - see
// DebugService::Context()).
//
//===----------------------------------------------------------------------===//

#include "dap/debug_service_factory.hpp"

#include "core/debug_context.hpp"
#include "core/event_bus.hpp"
#include "dap/dap_log.hpp"
#include "dap/orchestrator.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "debug_service/debug_service.hpp"
#include "handlers/register_handlers.hpp"
#include <variant>

namespace dap {

namespace {

class DebugServiceSessionImpl final : public DebugServiceSession {
public:
    explicit DebugServiceSessionImpl(Orchestrator &orchestrator)
        : debug_service_(orchestrator, context_.Lldb(), context_),
          handlers_(RegisterDebugHandlers(orchestrator, debug_service_)) {
        WireContext(orchestrator);
    }

private:
    // Forwarding seams for the communication-layer/not-yet-carved state
    // ExecutionController's and TargetManager's DebugContext-mediated calls
    // still reach for - see core::DebugContext's class comment. Removed one
    // by one as the owning component is carved (RunStopCommands,
    // RunExitCommands and HasLastLaunchRequest were the last ones TargetManager
    // absorbed - they're now direct DebugContext forwards, no seam needed).
    void WireContext(Orchestrator &orchestrator) {
        context_.SetSendTerminatedEvent([this] { debug_service_.SendTerminatedEvent(); });
        // TargetManager::Disconnect() needs to stop the Orchestrator's read
        // loop - a pure communication-layer action core has no other way to
        // reach. Safe to capture orchestrator by reference: the Orchestrator
        // outlives this session (see dap_main.cc).
        context_.SetRequestStop([&orchestrator] { orchestrator.RequestStop(); });
        context_.SetSetFrameFormat([this](llvm::StringRef format) { debug_service_.SetFrameFormat(format); });
        context_.SetSetThreadFormat([this](llvm::StringRef format) { debug_service_.SetThreadFormat(format); });
        context_.SetLogDiagnostic([this](std::string message) {
            DAP_LOG(debug_service_.log, "{0}", message);
        });
        context_.SetClientName([this] { return debug_service_.GetClientName(); });
        context_.SetClientFeatureEnabled(
            [this](dap::protocol::ClientFeature feature) { return debug_service_.clientFeatures.contains(feature); });
        context_.SetGetCustomCapabilities([this] { return debug_service_.GetCustomCapabilities(); });

        // The event_translator role described in core/event_bus.hpp's class
        // comment: the sole EventBus subscriber, converting each domain
        // event to the matching wire write. Both arms forward to
        // DebugService methods that already implement the real logic
        // (category-string mapping/line-splitting for output, seq
        // assignment/interrupt-cancellation for messages) - see
        // DebugService::SendOutput/Send - so there's nothing to duplicate
        // here, just the translation from core's vocabulary to DebugService's.
        context_.Events().Subscribe([this](const core::DomainEvent &event) {
            std::visit([this](const auto &domain_event) { HandleDomainEvent(domain_event); }, event);
        });
    }

    void HandleDomainEvent(const core::OutputEvent &event) {
        OutputType type;
        switch (event.category) {
        case core::OutputCategory::Console: type = OutputType::Console; break;
        case core::OutputCategory::Important: type = OutputType::Important; break;
        case core::OutputCategory::Stdout: type = OutputType::Stdout; break;
        case core::OutputCategory::Stderr: type = OutputType::Stderr; break;
        case core::OutputCategory::Telemetry: type = OutputType::Telemetry; break;
        }
        debug_service_.SendOutput(type, event.text);
    }

    void HandleDomainEvent(const core::WireMessageEvent &event) { debug_service_.Send(event.message); }

    // Declaration order matters: context_ must be constructed before
    // debug_service_, which binds reference members to context_'s
    // LldbProvider (see DebugService::debugger/target) and holds a
    // DebugContext& of its own.
    core::DebugContext context_;
    DebugService debug_service_;
    std::vector<std::unique_ptr<BaseRequestHandler>> handlers_;
};

}  // namespace

std::unique_ptr<DebugServiceSession> CreateDebugServiceSession(dap::Orchestrator &orchestrator) {
    return std::make_unique<DebugServiceSessionImpl>(orchestrator);
}

}  // namespace dap
