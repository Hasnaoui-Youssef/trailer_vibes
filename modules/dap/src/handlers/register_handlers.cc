#include "handlers/register_handlers.hpp"

#include "dap/orchestrator.hpp"

namespace dap {

namespace {

template <typename Handler>
void Register(Orchestrator &orchestrator, DebugService &dap, std::vector<std::unique_ptr<BaseRequestHandler>> &handlers) {
    auto handler = std::make_unique<Handler>(dap);
    orchestrator.RegisterHandler(Handler::GetCommand().str(), *handler);
    handlers.push_back(std::move(handler));
}

}  // namespace

std::vector<std::unique_ptr<BaseRequestHandler>> RegisterDebugHandlers(Orchestrator &orchestrator, DebugService &dap) {
    std::vector<std::unique_ptr<BaseRequestHandler>> handlers;

    Register<InitializeRequestHandler>(orchestrator, dap, handlers);
    Register<AttachRequestHandler>(orchestrator, dap, handlers);
    Register<ConfigurationDoneRequestHandler>(orchestrator, dap, handlers);
    Register<ThreadsRequestHandler>(orchestrator, dap, handlers);
    Register<StackTraceRequestHandler>(orchestrator, dap, handlers);
    Register<ContinueRequestHandler>(orchestrator, dap, handlers);
    Register<PauseRequestHandler>(orchestrator, dap, handlers);
    Register<DisconnectRequestHandler>(orchestrator, dap, handlers);

    Register<BreakpointLocationsRequestHandler>(orchestrator, dap, handlers);
    Register<CancelRequestHandler>(orchestrator, dap, handlers);
    Register<CompileUnitsRequestHandler>(orchestrator, dap, handlers);
    Register<CompletionsRequestHandler>(orchestrator, dap, handlers);
    Register<DataBreakpointInfoRequestHandler>(orchestrator, dap, handlers);
    Register<DisassembleRequestHandler>(orchestrator, dap, handlers);
    Register<EvaluateRequestHandler>(orchestrator, dap, handlers);
    Register<ExceptionInfoRequestHandler>(orchestrator, dap, handlers);
    Register<LaunchRequestHandler>(orchestrator, dap, handlers);
    Register<LocationsRequestHandler>(orchestrator, dap, handlers);
    Register<ModulesRequestHandler>(orchestrator, dap, handlers);
    Register<ModuleSymbolsRequestHandler>(orchestrator, dap, handlers);
    Register<NextRequestHandler>(orchestrator, dap, handlers);
    Register<ReadMemoryRequestHandler>(orchestrator, dap, handlers);
    Register<RestartRequestHandler>(orchestrator, dap, handlers);
    Register<ScopesRequestHandler>(orchestrator, dap, handlers);
    Register<SetBreakpointsRequestHandler>(orchestrator, dap, handlers);
    Register<SetDataBreakpointsRequestHandler>(orchestrator, dap, handlers);
    Register<SetExceptionBreakpointsRequestHandler>(orchestrator, dap, handlers);
    Register<SetFunctionBreakpointsRequestHandler>(orchestrator, dap, handlers);
    Register<SetInstructionBreakpointsRequestHandler>(orchestrator, dap, handlers);
    Register<SetVariableRequestHandler>(orchestrator, dap, handlers);
    Register<SourceRequestHandler>(orchestrator, dap, handlers);
    Register<StepInRequestHandler>(orchestrator, dap, handlers);
    Register<StepInTargetsRequestHandler>(orchestrator, dap, handlers);
    Register<StepOutRequestHandler>(orchestrator, dap, handlers);
    Register<TestGetTargetBreakpointsRequestHandler>(orchestrator, dap, handlers);
    Register<VariablesRequestHandler>(orchestrator, dap, handlers);
    Register<WriteMemoryRequestHandler>(orchestrator, dap, handlers);

    // Fallback for any command not owned by another handler - see
    // dap::Orchestrator::HandleRequest's own "unrecognized request" path,
    // which this pre-empts for any command a client might send that isn't
    // in the DAP spec at all but that DebugService still wants to answer
    // with a well-formed (if unsupported) response instead of a bare error.
    Register<UnknownRequestHandler>(orchestrator, dap, handlers);

    return handlers;
}

}  // namespace dap
