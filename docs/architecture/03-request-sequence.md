# Request sequence

Dispatch is a reader thread and a dispatch thread, not one thread reading
and handling in lockstep. `Orchestrator::Run()` starts `ReaderLoop` on its
own thread and runs `DispatchLoop` on the caller's:

- **`ReaderLoop`**: blocks on `Transport::ReadMessage()`, decodes each frame
  into a `protocol::Request`, and pushes it onto a queue. A `cancel` request
  is handled specially here, before it's even enqueued: its target request
  ID is inserted into `cancelled_requests_` immediately, so a request still
  sitting in the queue (or already dispatched) can observe cancellation
  without waiting for the `cancel` message itself to reach the front of the
  queue.
- **`DispatchLoop`**: pops one request at a time and calls `HandleRequest`,
  which looks it up in `command_handlers_` and calls `Run`. Still exactly
  one dispatch thread - handler bodies never run concurrently with each
  other - but reading and dispatching are no longer serialized against each
  other the way they used to be.

```mermaid
sequenceDiagram
    participant Client
    participant Transport
    participant Reader as ReaderLoop (own thread)
    participant Dispatch as DispatchLoop
    participant BaseHandler as BaseRequestHandler
    participant Handler as RequestHandler<Args,Resp>
    participant Core as core:: (manager, or raw SB API in a few handlers)

    Client->>Transport: Content-Length frame
    Transport->>Reader: ReadMessage()
    Reader->>Reader: fromJSON -> protocol::Request
    Reader->>Reader: if command == "cancel": mark cancelled_requests_ now
    Reader->>Dispatch: push onto request_queue_, notify
    Dispatch->>BaseHandler: command_handlers_[cmd]->Run(request)
    BaseHandler->>Dispatch: IsCancelled(request)?
    alt cancelled
        BaseHandler->>Dispatch: Send(cancelled response)
    else not cancelled
        BaseHandler->>Handler: operator()(request)
        Handler->>Handler: parseArgs<Args>(request)
        Handler->>Core: Run(*arguments) - virtual dispatch
        Note over Core: most methods: WithTarget(callable)<br/>- one encapsulated critical section
        Note over Core: a handful of handlers still drive<br/>lldb::SB* directly in the handler body
        Core-->>Handler: llvm::Expected<ResponseBody> or Error
        Handler->>BaseHandler: SendSuccess/SendError
        BaseHandler->>Dispatch: Send(response)
        Dispatch->>Transport: WriteMessage
        Transport->>Client: Content-Length frame
    end
```

## Deferred response: launch/attach

`launch` and `attach` use `DelayedResponseRequestHandler` instead of
`RequestHandler`, since the DAP spec expects `initialized` (and the
client's follow-up breakpoint/configuration requests) to happen *between*
the request and its response:

```mermaid
sequenceDiagram
    participant Client
    participant Dispatch
    participant LaunchHandler as DelayedResponseRequestHandler
    participant ConfigDone as ConfigurationDoneRequestHandler

    Client->>Dispatch: launch request
    LaunchHandler->>LaunchHandler: Run(*arguments) (builds response, not sent yet)
    LaunchHandler->>Dispatch: SetDeferredConfigurationResponse(fn)
    LaunchHandler->>Dispatch: Send(Event{"initialized"})
    Client->>Dispatch: (breakpoints etc, then) configurationDone request
    ConfigDone->>ConfigDone: Run(args) -> PostRun()
    ConfigDone->>Dispatch: RunDeferredConfigurationResponse()
    Dispatch->>Client: launch response (finally sent)
```

`launch` additionally embeds OpenOCD in-process at this point
(`DebugContext::CreateOpenOcd`, called from `TargetManager::Launch`) - this
is what backs memory/peripheral watches and instruction-trace capture for
the rest of the session. `attach` never creates one; features that need
`context.OpenOcd()` are unavailable under `attach`.

## The locking picture on this path

`BaseRequestHandler::Run` takes no lock itself - every `core` method a
handler calls is expected to lock for its own duration via
`LldbProvider::WithTarget`, not the caller. This holds cleanly for the
methods that actually route through `WithTarget` (most of `core`'s manager
methods, `~15` source files at last count).

It does **not** hold for two specific accessors and the handlers that use
them: `DebugContext::GetLLDBThread`/`GetLLDBFrame` call
`SBProcess`/`SBThread` methods directly, with no `WithTarget` wrapper - and
several handlers (`variables`, `evaluate`, `scopes`, `stackTrace`,
`threads`, most of `symbols/*`) call one of those two accessors and then
keep driving the returned `SBThread`/`SBFrame` themselves. This is the one
real carryover from the pre-`WithTarget` locking review: the redesign made
locking for participating code encapsulated and impossible to get wrong,
but it didn't close this specific pre-existing gap, because closing it
means moving the SB traversal those handlers do out of the handler and into
a `core` method in the first place - a larger change than the locking
redesign itself. See `05-known-characteristics.md`.
