# Request sequence

One request, wire to wire. Dispatch is single-threaded: `Orchestrator::Run`
reads one frame, fully handles it (response included), then reads the next
(`orchestrator.cc:40-57`). The API mutex is never taken on this path itself -
only inside individual `core` methods, inconsistently (see note below and
`05-limitations.md` #1).

```mermaid
sequenceDiagram
    participant Client
    participant Transport
    participant Orchestrator
    participant BaseHandler as BaseRequestHandler
    participant Handler as RequestHandler<Args,Resp>
    participant Core as core:: (manager or raw SB API)

    Client->>Transport: Content-Length frame
    Transport->>Orchestrator: ReadMessage() (transport.cc:48)
    Orchestrator->>Orchestrator: fromJSON -> protocol::Request (orchestrator.cc:47-53)
    Orchestrator->>Orchestrator: HandleRequest (:59), active_request_ bookkeeping (:64-71)
    Orchestrator->>BaseHandler: command_handlers_[cmd]->Run(request) (:77-79)
    BaseHandler->>Orchestrator: IsCancelled(request)? (request_handler.cc:42)
    alt cancelled
        BaseHandler->>Orchestrator: Send(cancelled response)
    else not cancelled
        BaseHandler->>BaseHandler: CancelInterruptRequest() (:52)
        BaseHandler->>Handler: operator()(request) (:58, no lock taken here)
        Handler->>Handler: parseArgs<Args>(request) (request_handler.hpp:116)
        Handler->>Core: Run(*arguments) - virtual dispatch (:121/:126)
        Note over Core: clean path: ONE core method,<br/>locks GetAPIMutex internally<br/>(inconsistently - see note)
        Note over Core: fat-handler path: handler body<br/>drives lldb::SB* directly, unlocked
        Core-->>Handler: llvm::Expected<ResponseBody> or Error
        Handler->>BaseHandler: SendSuccess/SendError (:124/:130)
        BaseHandler->>BaseHandler: IsInterruptRequested()? mark cancelled (request_handler.cc:174-178)
        BaseHandler->>Orchestrator: Send(response)
        Orchestrator->>Orchestrator: assign seq under send_mutex_ (:92-102)
        Orchestrator->>Transport: WriteMessage
        Transport->>Client: Content-Length frame
    end
```

## Deferred response: launch/attach

`launch` and `attach` use `DelayedResponseRequestHandler` instead of
`RequestHandler`:

```mermaid
sequenceDiagram
    participant Client
    participant Orchestrator
    participant LaunchHandler as DelayedResponseRequestHandler
    participant ConfigDone as ConfigurationDoneRequestHandler

    Client->>Orchestrator: launch request
    Orchestrator->>LaunchHandler: Run -> operator()
    LaunchHandler->>LaunchHandler: Run(*arguments) (builds response, not sent yet)
    LaunchHandler->>Orchestrator: SetDeferredConfigurationResponse(fn) (request_handler.hpp:168)
    LaunchHandler->>Orchestrator: Send(Event{"initialized"}) (:173-174)
    Client->>Orchestrator: (breakpoints etc, then) configurationDone request
    Orchestrator->>ConfigDone: Run -> operator() -> Run(args) -> PostRun()
    ConfigDone->>Orchestrator: RunDeferredConfigurationResponse() (configuration_done_request_handler.cc:41)
    Orchestrator->>Client: launch response (finally sent)
```

## The API-mutex gap on this path

`BaseRequestHandler::Run`'s own comment states the contract:
"No API-mutex lock here: every core method a handler calls below (via
`operator()`) locks internally for its own duration"
(`request_handler.cc:54-57`). This holds for most `core` managers (the
`GetAPIMutex`/`lock_guard` idiom repeated per method, `05-limitations.md`
#4), but is violated in two independent ways:

1. Fat handlers (`variables`, `evaluate`, `scopes`, `stackTrace`, `threads`,
   most `symbols/*`) skip `core` methods for their SB traversal and touch
   `lldb::SB*` directly in the handler body - no lock exists at any level for
   that code. Example: `scopes_request_handler.cc:18`
   `lldb::SBFrame frame = context_.GetLLDBFrame(args.frameId);` - unlocked
   both in `GetLLDBFrame` (`debug_context.cc:52-58`, no lock) and in the
   handler using the returned frame.
2. Even a "clean delegator" can reach an unlocked `core` method:
   `MemoryManager`/`ProcessMemoryStrategy::Read`/`Write`
   (`memory_manager.cc:13-70`) never take `GetAPIMutex` at all, and
   `read_memory_request_handler.cc:24`
   (`lldb::SBProcess process = context_.Target().GetProcess();`) reads the
   target directly in the handler, also unlocked. "Delegates to one core
   method" is not, on its own, evidence of safety - see
   `05-limitations.md` #1.
