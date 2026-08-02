#include "handlers/register_handlers.hpp"

#include "dap/orchestrator.hpp"

namespace dap {

namespace {

template <typename Handler>
void Register(Orchestrator &orchestrator, core::DebugContext &context, std::vector<std::unique_ptr<BaseRequestHandler>> &handlers) {
    auto handler = std::make_unique<Handler>(orchestrator, context);
    orchestrator.RegisterHandler(Handler::GetCommand().str(), *handler);
    handlers.push_back(std::move(handler));
}

}  // namespace

std::vector<std::unique_ptr<BaseRequestHandler>> RegisterDebugHandlers(Orchestrator &orchestrator, core::DebugContext &context) {
    std::vector<std::unique_ptr<BaseRequestHandler>> handlers;

    Register<InitializeRequestHandler>(orchestrator, context, handlers);
    Register<AttachRequestHandler>(orchestrator, context, handlers);
    Register<ConfigurationDoneRequestHandler>(orchestrator, context, handlers);
    Register<ThreadsRequestHandler>(orchestrator, context, handlers);
    Register<StackTraceRequestHandler>(orchestrator, context, handlers);
    Register<ContinueRequestHandler>(orchestrator, context, handlers);
    Register<PauseRequestHandler>(orchestrator, context, handlers);
    Register<DisconnectRequestHandler>(orchestrator, context, handlers);

    Register<BreakpointLocationsRequestHandler>(orchestrator, context, handlers);
    Register<CancelRequestHandler>(orchestrator, context, handlers);
    Register<CompileUnitsRequestHandler>(orchestrator, context, handlers);
    Register<CompletionsRequestHandler>(orchestrator, context, handlers);
    Register<DataBreakpointInfoRequestHandler>(orchestrator, context, handlers);
    Register<DisassembleRequestHandler>(orchestrator, context, handlers);
    Register<EvaluateRequestHandler>(orchestrator, context, handlers);
    Register<ExceptionInfoRequestHandler>(orchestrator, context, handlers);
    Register<LaunchRequestHandler>(orchestrator, context, handlers);
    Register<LocationsRequestHandler>(orchestrator, context, handlers);
    Register<ModulesRequestHandler>(orchestrator, context, handlers);
    Register<ModuleSymbolsRequestHandler>(orchestrator, context, handlers);
    Register<NextRequestHandler>(orchestrator, context, handlers);
    Register<ReadMemoryRequestHandler>(orchestrator, context, handlers);
    Register<RestartRequestHandler>(orchestrator, context, handlers);
    Register<ScopesRequestHandler>(orchestrator, context, handlers);
    Register<SetBreakpointsRequestHandler>(orchestrator, context, handlers);
    Register<SetDataBreakpointsRequestHandler>(orchestrator, context, handlers);
    Register<SetExceptionBreakpointsRequestHandler>(orchestrator, context, handlers);
    Register<SetFunctionBreakpointsRequestHandler>(orchestrator, context, handlers);
    Register<SetInstructionBreakpointsRequestHandler>(orchestrator, context, handlers);
    Register<SetVariableRequestHandler>(orchestrator, context, handlers);
    Register<SourceRequestHandler>(orchestrator, context, handlers);
    Register<StepInRequestHandler>(orchestrator, context, handlers);
    Register<StepInTargetsRequestHandler>(orchestrator, context, handlers);
    Register<StepOutRequestHandler>(orchestrator, context, handlers);
    Register<TestGetTargetBreakpointsRequestHandler>(orchestrator, context, handlers);
    Register<TraceEnableRequestHandler>(orchestrator, context, handlers);
    Register<TraceDisableRequestHandler>(orchestrator, context, handlers);
    Register<TraceStatusRequestHandler>(orchestrator, context, handlers);
    Register<VariablesRequestHandler>(orchestrator, context, handlers);
    Register<WatchStartRequestHandler>(orchestrator, context, handlers);
    Register<WatchStopRequestHandler>(orchestrator, context, handlers);
    Register<WriteMemoryRequestHandler>(orchestrator, context, handlers);
    Register<DeviceInfoRequestHandler>(orchestrator, context, handlers);
    Register<PeripheralDetailRequestHandler>(orchestrator, context, handlers);
    Register<PeripheralReadRequestHandler>(orchestrator, context, handlers);
    Register<PeripheralWriteRequestHandler>(orchestrator, context, handlers);
    Register<PeripheralWatchStartRequestHandler>(orchestrator, context, handlers);
    Register<PeripheralWatchStopRequestHandler>(orchestrator, context, handlers);

    Register<UnknownRequestHandler>(orchestrator, context, handlers);

    return handlers;
}

}  // namespace dap
