#include "dap/session.hpp"

#include "core/debug_context.hpp"
#include "core/event_bus.hpp"
#include "dap/dap_log.hpp"
#include "dap/orchestrator.hpp"
#include "dap/protocol/protocol_base.hpp"
#include "dap/protocol/protocol_requests.hpp"
#include "dap/json_utils.hpp"
#include "handlers/capabilities.hpp"
#include "handlers/register_handlers.hpp"
#include "llvm/Support/JSON.h"
#include <mutex>
#include <variant>

namespace dap {

namespace {

std::mutex g_log_mutex;

class SessionImpl final : public Session {
public:
    explicit SessionImpl(Orchestrator &orchestrator)
        : orchestrator_(orchestrator),
          log_(llvm::errs(), g_log_mutex),
          handlers_(RegisterDebugHandlers(orchestrator, context_)) {
        WireContext();
    }

private:
    void WireContext() {
        // Binding disconnect to stop the Orchestrator
        context_.SetRequestStop([this] { orchestrator_.RequestStop(); });
        context_.SetLogDiagnostic([this](std::string message) { DAP_LOG(log_, "{0}", message); });
        context_.SetClientName([this] { return orchestrator_.ClientName(); });
        context_.SetClientFeatureEnabled(
            [this](dap::protocol::ClientFeature feature) { return orchestrator_.ClientFeatureEnabled(feature); });
        context_.SetGetCustomCapabilities([this] { return AssembleCustomCapabilities(orchestrator_); });

        context_.Events().Subscribe([this](const core::DomainEvent &event) {
            std::visit([this](const auto &domain_event) { HandleDomainEvent(domain_event); }, event);
        });
    }

    void HandleDomainEvent(const core::OutputEvent &event) {
        if (event.text.empty())
            return;

        const char *category = nullptr;
        switch (event.category) {
        case core::OutputCategory::Console: category = "console"; break;
        case core::OutputCategory::Important: category = "important"; break;
        case core::OutputCategory::Stdout: category = "stdout"; break;
        case core::OutputCategory::Stderr: category = "stderr"; break;
        case core::OutputCategory::Telemetry: category = "telemetry"; break;
        }

        llvm::StringRef output = event.text;
        size_t idx = 0;
        do {
            size_t end = output.find('\n', idx);
            if (end == llvm::StringRef::npos)
                end = output.size() - 1;
            llvm::json::Object body;
            body.try_emplace("category", category);
            EmplaceSafeString(body, "output", output.slice(idx, end + 1).str());
            orchestrator_.Send(dap::protocol::Event{"output", llvm::json::Value(std::move(body))});
            idx = end + 1;
        } while (idx < output.size());
    }

    template <typename EventBody>
    void SendTypedEvent(llvm::StringRef name, EventBody body) {
        orchestrator_.Send(dap::protocol::Event{name.str(), std::move(body)});
    }

    void HandleDomainEvent(const core::StoppedEvent &event) { SendTypedEvent("stopped", event.body); }
    void HandleDomainEvent(const core::ContinuedEvent &event) { SendTypedEvent("continued", event.body); }
    void HandleDomainEvent(const core::ExitedEvent &event) { SendTypedEvent("exited", event.body); }
    void HandleDomainEvent(const core::ThreadExitedEvent &event) { SendTypedEvent("thread", event.body); }
    void HandleDomainEvent(const core::ProcessEvent &event) { SendTypedEvent("process", event.body); }
    void HandleDomainEvent(const core::CapabilitiesEvent &event) { SendTypedEvent("capabilities", event.body); }
    void HandleDomainEvent(const core::InvalidatedEvent &event) { SendTypedEvent("invalidated", event.body); }
    void HandleDomainEvent(const core::MemoryEvent &event) { SendTypedEvent("memory", event.body); }
    void HandleDomainEvent(const core::ModuleEvent &event) { SendTypedEvent("module", event.body); }
    void HandleDomainEvent(const core::BreakpointEvent &event) { SendTypedEvent("breakpoint", event.body); }

    void HandleDomainEvent(const core::TerminatedEvent &event) {
        dap::protocol::Event evt{"terminated"};
        if (!event.statistics_json.empty()) {
            if (llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(event.statistics_json)) {
                evt.body = llvm::json::Object{{"$__lldb_statistics", std::move(*parsed)}};
            } else {
                DAP_LOG(log_, "failed to re-parse terminated-event statistics: {0}",
                        llvm::toString(parsed.takeError()));
            }
        }
        orchestrator_.Send(evt);
    }

    // Declaration order matters: context_ must be constructed before handlers_, fix me?
    Orchestrator &orchestrator_;
    Log log_;
    core::DebugContext context_;
    std::vector<std::unique_ptr<BaseRequestHandler>> handlers_;
};

}  // namespace

std::unique_ptr<Session> CreateSession(dap::Orchestrator &orchestrator) {
    return std::make_unique<SessionImpl>(orchestrator);
}

}  // namespace dap
