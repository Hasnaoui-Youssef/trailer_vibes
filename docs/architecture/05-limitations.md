# Limitations: honest review

Ranked most-severe first. Each entry: symptom, anchor, impact, one-line
recommended direction. This is a review, not a redesign - direction lines
name a goal, not an implementation. Executing any of them is a separate plan.

## 1. Unlocked SB access under a real second thread - data race

`lldb_provider.hpp:32-34` states the mutex's exact purpose: "Serializes
access to the debugger/target across threads (the DAP request-handling
thread and the event thread)." Both threads are real and long-lived
(`ExecutionController::m_event_thread`, `execution_controller.cc:830-881`).
Two independent gaps defeat the stated invariant:

- **Fat handlers bypass `core` entirely.** `stack_trace_request_handler.cc:198`
  `lldb::SBThread thread = context_.GetLLDBThread(args.threadId);`,
  `variables_request_handler.cc:47-141`, `evaluate_request_handler.cc:33-100`,
  `scopes_request_handler.cc:18-36` all call `DebugContext::GetLLDBThread`/
  `GetLLDBFrame` (`debug_context.cc:48-58`, no lock in either) and then drive
  the returned `SBThread`/`SBFrame`/`SBValue` directly, with no lock held for
  the duration of the traversal.
- **A "clean delegator" is not proof of safety.** `read_memory_request_handler.cc:24`
  reads `context_.Target().GetProcess()` directly in the handler (unlocked),
  and the one `core` call it does make -
  `MemoryManager::ReadMemory`/`ProcessMemoryStrategy::Read`/`Write`
  (`memory_manager.cc:13-70`) - never takes `GetAPIMutex` at all. Delegation
  to `core` only helps if the specific manager locks; not all of them do.

Impact: concurrent SB API access from the event thread and the request
thread is undefined behavior in LLDB's SB API contract, not just a style
smell. This is the most severe finding - a correctness bug, latent until a
debug session under load exercises both threads at once. Direction: no
handler ever holds a raw SB object; every manager method that touches
`SBTarget`/`SBProcess`/`SBThread`/`SBFrame`/`SBValue` locks internally,
without exception, including `MemoryManager`.

## 2. Handlers drive the SB API instead of delegating (layer violation)

`debug_context.hpp:193-196`'s own comment states the rule: "no layer above
core may name an lldb:: type... no lock is ever exposed above core." Violated
by the same file two dozen lines above it: `Target()` returns
`lldb::SBTarget&` (`:76`), `GetLLDBThread`/`GetLLDBFrame` return
`lldb::SBThread`/`SBFrame` (`:105-106`), `ResolveSource(const lldb::SBFrame&)`
takes one (`:124`), `GetAPIMutex()` returns `lldb::SBMutex` (`:77`). ~15
handler `.cc` files consume these: `variables` (~145 lines of SB traversal,
dedup naming, return-value synthesis, `variables_request_handler.cc:25-170`),
`stackTrace` (frame/format/instruction-list logic,
`stack_trace_request_handler.cc:31-245`), `evaluate` (REPL-mode detection,
expression eval, memory-ref packing, `evaluate_request_handler.cc:31-108`),
`scopes`, `threads`, `exceptionInfo`, `completions`, `setVariable`, and most
`symbols/*` handlers. Impact: the handler layer is not actually LLDB-
abstracted despite being documented as such; any future frontend reusing
`core` still needs `dap_handlers`' logic rewritten, not just its transport.
Direction: each handler becomes parse-args -> one `core` method returning
`llvm::Expected<protocol::*ResponseBody>` -> format-response; the SB
traversal currently inline in handlers moves into new or existing `core`
methods.

## 3. DebugContext leaks implementation details

Beyond #2's specific accessors: `Lldb()` returns the whole
`providers::LldbProvider&` (`:75`), `GetSourceReferenceAddress` returns
`std::optional<lldb::addr_t>` (`:126`), `RestartingProcessId`/
`SetRestartingProcessId` are `lldb::pid_t` (`:139-140`). Every one of these
is a hole in the "core exposes only protocol-typed operations" boundary the
rest of the class tries to hold (contrast with `:201-207`'s plain accessors,
which correctly hide `lldb::` types and lock internally). Impact: callers
above `core` can always reach further into LLDB than the architecture
intends, one accessor at a time - the boundary is unenforceable as long as
any lldb-typed accessor exists, since new code can always pick the leaky one
instead of the narrow one. Direction: delete every lldb-typed accessor;
route each caller through a protocol-typed equivalent.

## 4. Locking model: manual per-method copy-paste

`GetAPIMutex` chains `LldbProvider::GetAPIMutex()`
(`lldb_provider.hpp:35`, `target.GetAPIMutex()`) -> `DebugContext::GetAPIMutex()`
(`debug_context.hpp:77`, a straight forward) -> every manager method that
needs it, each hand-writing:

```cpp
lldb::SBMutex lock = m_context.GetAPIMutex();
std::lock_guard<lldb::SBMutex> guard(lock);
```

Counted at ~25 sites: `debug_context.cc` (4, self-copies), `breakpoint.cc`,
`function_breakpoint.cc`, `source_breakpoint.cc`, `exception_breakpoint.cc`
(1 each), `breakpoint_manager.cc` (5), `target_manager.cc` (4),
`execution_controller.cc` (8), plus a free function in
`execution_controller.cc:71`. Impact: the resource being serialized (the
`SBTarget`) is owned by `LldbProvider`, but the serialization discipline is
re-derived by every caller instead of being a property of the resource - the
copy-paste is both a readability tax and the reason #1 and #5 are possible
(it is easy to write a new method and simply forget the two lines, or get
their placement wrong relative to the SB calls). Direction: encapsulate
serialization inside `LldbProvider` itself - a `WithTarget(callable)` scope
or a provider-returned RAII guarded accessor - so there is no lock object for
a caller to forget, misplace, or expose; remove `GetAPIMutex()` from
`DebugContext` entirely once nothing needs it directly.

## 5. Lock/no-lock overload hazard, plus two adjacent hazards

`BreakpointManager::SetSourceBreakpoints` has two overloads distinguished
only by an extra argument:

- 2-arg (public) **locks**: `breakpoint_manager.cc:115-119`
  (`lldb::SBMutex lock = m_context.GetAPIMutex(); std::lock_guard... guard(lock);`).
- 3-arg (private) **assumes the lock is already held**:
  `breakpoint_manager.cc:136-139` (declared `breakpoint_manager.hpp:89-92`),
  touches `m_context.Target().BreakpointDelete(...)` at `:183` with no lock
  of its own. Called only from inside the already-locked 2-arg overload
  today (`:125,130`) - safe by convention, not by anything the compiler
  checks. Any future caller of the 3-arg overload directly reintroduces the
  race from #1 silently.

Two related hazards in the same lock family:

- **Recursive-lock reliance.** `TargetManager::Launch`
  (`target_manager.cc:402-404`) takes the lock, then calls `LaunchProcess`
  (`:438`) which takes it again (`:221-223`, via
  `BaseRequestHandler::LaunchProcess` ->
  `TargetManager::LaunchProcess`). Correct only because `SBMutex` happens to
  be recursive; nothing marks either function as "must be called under the
  lock" or "safe to re-enter."
- **Two-mutex ordering.** `execution_controller.cc:722-726` takes both the
  API mutex and `ModuleManager::modules_mutex` via `std::scoped_lock`, with a
  comment warning "both mutexes must be acquired to prevent deadlock when
  handling `modules_request`, which also requires both locks" - correct
  today only because both call sites happen to acquire them in the same
  relative order, enforced by convention and a comment, not by structure.

Direction: fold all three into the #4 redesign - encapsulated locking with
no overload split, no caller-visible lock object to hold across a nested
call, and no second, independently-locked resource (`modules_mutex`) that a
caller must remember to order correctly against the API mutex.

## 6. debug_service/ not deleted

Fork remnant, still built and linked: `trailer-dap` links it directly
(`engine/CMakeLists.txt:98`), `dap_handlers` links it too
(`modules/dap/CMakeLists.txt:187`). 21 handler files still
`#include "debug_service/..."` for leftover free functions
(`variables.hpp`, `json_utils.hpp`, `lldb_utils.hpp`) - e.g.
`stack_trace_request_handler.cc:12,14`, `variables_request_handler.cc:10,12,13`,
`evaluate_request_handler.cc:11,12,13`. `ThreadHasStopReason` and
`GetStringValue` exist in two copies: a live one in
`execution_controller.cc:87-110,112-118` (called `:544,570` and `:820-821`
respectively) and a dead one in `debug_service/lldb_utils.{hpp,cc}`.
Impact: two sources of truth for the same helpers, and every new handler
faces a choice between the fork's helpers and `core`'s. Direction: rehome
the still-live helpers into `core`/`dap_protocol`, delete
`modules/dap/src/debug_service/` and its CMake target and every
`#include "debug_service/..."`.

## 7. DebugContext god-object

Owns `LldbProvider` and `EventBus` by value, 7 managers by `unique_ptr`,
mirrored config (`auto_variable_summaries_`, `command_escape_prefix_`), and 5
`std::function` seams (`debug_context.hpp:210-226`). Two managers
(`MemoryManager`, `ModuleManager`) don't even go through it for peer access -
they hold `LldbProvider&` directly (`memory_manager.hpp:66,71`,
`debug_context.cc:24,26`), while the other 5 hold `DebugContext&` - an
inconsistent mediator pattern where some components can reach every peer and
two can reach none. Impact: one class accumulates orchestration logic
(`RunStopCommands`/`RunExitCommands`, `SendTerminatedEvent`,
`ResolveSource`) that arguably belongs to the owning manager, and the
inconsistent access pattern means "does this manager see its siblings" is a
per-manager fact a reader has to check rather than a rule. Direction: shrink
`DebugContext` to a pure mediator (construction + peer lookup only); every
manager holds `DebugContext&` uniformly; push config and orchestration
methods to the manager that owns the underlying state.

## 8. Dead abstractions and code

- `Service` / `Orchestrator::RegisterService` / `command_owners_`
  (`include/dap/service.hpp:17`, `orchestrator.cc:15-22`,
  checked in `HandleRequest` at `:82-89`) - a second, coarser dispatch
  abstraction alongside `IRequestHandler`. Zero registrations anywhere in the
  tree (confirmed: `RegisterService` has no call sites).
- Reverse-request machinery, two copies: `Orchestrator::SendReverseRequest` +
  `inflight_reverse_requests_`/`reverse_requests_mutex_`
  (`orchestrator.hpp:129-134,156-157`) and a second copy in the legacy
  `debug_service.hpp:86,113-116`. Zero callers in either.
- `DebugContext::ReadMemoryRequest`/`WriteMemoryRequest`
  (`debug_context.hpp:115-118`) - declared with a doc comment claiming
  internal locking and stopped-state validation, but never defined in
  `core/src` and never called; the real path is
  `context_.Memory().ReadMemory(...)` called directly from the handler
  (`read_memory_request_handler.cc:28`).
- `GetEnvironmentFromArguments`, `TelemetryDispatcher` - exist only in
  `debug_service/lldb_utils.{hpp,cc}`, no callers anywhere.
- Stale comment: `modules/dap/CMakeLists.txt:178-180` claims `dap_main.cc`
  needs a `DebugServiceSession` declared in `dap/debug_service_factory.hpp`;
  neither exists (confirmed by grep; only a stale `clangd` cache entry
  remains). The real seam is `dap::Session`/`dap::CreateSession` in
  `dap/session.hpp`.

Impact: dead code and a stale comment both cost a reader time confirming
they're inert, and the stale comment actively misdirects. Direction: delete
the dead code paths; fix or remove the stale comment.

## 9. Cancellation is a structural no-op

`Orchestrator::cancelled_requests_` is never populated -
`CancelRequestHandler::Run` only calls `ClearCancelRequest`
(`cancel_request_handler.cc:51`). Single-threaded read-then-dispatch
(`orchestrator.cc:40-57`) means a `cancel` message physically cannot arrive
until the request it would cancel has already finished
(`orchestrator.hpp:94-99`'s own comment confirms this). `initialize`
advertises `eAdapterFeatureCancelRequest` regardless (via
`CancelRequestHandler::GetSupportedFeatures`,
`request_handler.hpp:580-582`). Impact: a client is told cancellation works
and it structurally cannot. Direction: either stop advertising the
capability, or make it real via a background reader thread (a bigger change
than this review scopes).

## 10. Config and negotiation scatter

`DebugContext` mirrors `auto_variable_summaries_`/`command_escape_prefix_`
(`debug_context.hpp:212-213`) that `TargetManager::SetConfiguration` writes
through a setter (`target_manager.cc:93-94`) rather than owning outright.
`Orchestrator::ClientName()` returns the hardcoded literal `"trailer-dap"`
and is documented as such - "never actually set at runtime today... kept as
a method rather than inlined... so that changes, if any client negotiation
is added later, only happen here" (`orchestrator.hpp:108-113`). Capabilities
are assembled from three places: a static list
(`capabilities.cc:10-14`), per-handler `GetSupportedFeatures()` aggregated at
runtime (`:19`), and a separate custom-capabilities set
(`AssembleCustomCapabilities`, `:29-40`). Impact: three sources of truth for
"what can this adapter do," and one config value with two owners depending
on carve progress. Direction: single owner per config item (`TargetManager`
once carve completes); either implement real client-name negotiation or
delete the seam.

## 11. Wire body type-safety

`protocol::Event::body` and `protocol::Response::body` are
`std::optional<llvm::json::Value>` (`protocol_base.hpp:70,119`) - any handler
can put any JSON shape in an event or response body, checked only by
whatever `fromJSON`/consumer exists on the client side, not by the compiler.
`Response::message` carries a `// FIXME: Migrate usage of fallback string to
ErrorMessage` (`:106-111`) - two ways to report an error left live side by
side. Impact: a typo'd field name or wrong body shape for a given event kind
is a runtime/client-visible bug, not a build error. Direction: typed bodies
at the boundary (one struct per event/response kind, as already done for
`DomainEvent`'s `*EventBody` members) instead of `llvm::json::Value`.

## 12. Comment verbosity and no core unit tests

Header/source comment blocks are oversized tree-wide (multiple 10+ line
class comments per file, several quoted in this review). Verification today
is integration-only: handshake test plus hardware attach smoke test: no unit
tests exist for any `core` manager method. Impact: as logic moves out of
handlers and into `core` (per #2), there is no test harness to catch
regressions in the moved logic short of a full hardware run. Direction: trim
comments to non-obvious "why" only; add `core`-level unit tests as handler
logic migrates in, so each newly-added manager method ships with a test
alongside it.

---

## Trace/debug disjointness

The trace-analysis pipeline (`trailer` binary,
`modules/providers/trace/*`, OpenCSD) shares only `common` and
`modules/providers` at the CMake level with `trailer-dap`
(`00-overview.md`). It has none of this document's findings in scope: no
`DebugContext`, no `EventBus`, no LLDB dependency, no DAP transport. Any
future unification of the two executables under one binary (noted as future
work in `src/dap_main.cc:8-10`) is out of scope for this review.
