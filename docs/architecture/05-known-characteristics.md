# Known characteristics

Short, current, honest. Not a backlog - see `../../PROJECT_STATUS.md` for
what's actually planned next (packaging, trace-feature expansion). Most of
what used to fill this document (the `debug_service` fork remnant, the
per-method `GetAPIMutex` copy-paste, cancellation being a structural no-op,
the trace pipeline living in a disjoint CLI) has been resolved outright and
isn't repeated here just to say so - see `00-overview.md`'s "what changed"
section if that history is useful.

## Two accessors bypass the locking model

`LldbProvider::WithTarget` is the encapsulated critical section every
`core` manager method that touches LLDB SB API is expected to use. Two
`DebugContext` methods don't: `GetLLDBThread`/`GetLLDBFrame` call
`SBProcess`/`SBThread` directly, unlocked, and several handlers
(`variables`, `evaluate`, `scopes`, `stackTrace`, `threads`, most of
`symbols/*`) call one of those two accessors and then keep driving the
returned `SBThread`/`SBFrame` themselves, also unlocked. This is the one
concurrency gap the `WithTarget` redesign didn't close, because closing it
means moving each handler's SB traversal into a `core` method first - a
separate, larger change. Detail in `03-request-sequence.md` and
`04-event-dataflow.md`.

## `MemoryManager`/`ModuleManager` hold `LldbProvider&`, not `DebugContext&`

Every other component reaches its peers via `m_context.<Peer>()`. These two
hold the provider directly instead, wired that way at construction - they
can never see a sibling component even if a future feature needed them to.
Nothing currently needs that, so it's a latent inconsistency rather than an
active problem.

## `Service` is dead

`dap/service.hpp` still declares a `Service` class - a second, coarser
dispatch abstraction. `Orchestrator` no longer has any fallback path that
checks it (`command_owners_`/`RegisterService` are gone from
`orchestrator.{hpp,cc}`); the class is simply unused now, along with the one
`#include` that names it. Cheap to delete whenever someone's touching that
file for another reason.

## Wire event/response bodies are untyped at the boundary

`protocol::Event::body` and `protocol::Response::body` are
`std::optional<llvm::json::Value>` - any handler can put any JSON shape into
an event or response body, checked only by whatever the client does with
it, not by the compiler. `DomainEvent`'s `*EventBody` members (the `core`
side of the boundary) are fully typed; this characteristic is specifically
about the wire-serialization layer, where a typo'd field name for a given
event kind is a runtime bug, not a build error.

## Two-mutex ordering is convention, not structure

`ExecutionController::HandleTargetEvent` takes both `WithTarget`'s lock and
`ModuleManager::modules_mutex`, nested in a fixed order, with a comment
explaining why (`modules_request` acquires both in the same order). Correct
today because both call sites happen to agree on that order - nothing
prevents a future call site from acquiring them the other way round.

## Config lives partly on `DebugContext`, partly on `TargetManager`

`DebugContext` mirrors `auto_variable_summaries_`/`command_escape_prefix_`,
which `TargetManager::SetConfiguration` writes through a setter rather than
owning outright. Minor, but it means "which component actually owns this
setting" isn't answered the same way for every config value.
