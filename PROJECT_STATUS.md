# Project Status

Debug engine + trace-analysis pipeline. Two executables:

- `trailer-dap`: DAP adapter, LLDB-backed.
- `trailer`: offline trace-analysis CLI, OpenCSD-backed.

Full diagrams (layers, class relationships, request sequence, event
dataflow): `docs/architecture/`. This file: capabilities, pain points,
open work.

## Build

CMake + Ninja + Clang only. LLVM/Clang/LLDB from system packages.

```
cmake --preset linux
cmake --build build
```

Outputs `build/trailer-dap`, `build/trailer`.

## Layout

Layers, top to bottom:

```
dap_protocol (wire types, LLDB-free)
  <- dap_core (transport, orchestrator, dispatch)
    <- dap_handlers (~38 request handlers)
      <- core (7 domain components)
        <- lldb_provider (owns SBDebugger/SBTarget)
```

`common`: OS utilities, under `dap_protocol`. `providers/disassembler`,
`providers/trace/*` (OpenCSD pipeline): separate, linked only by `trailer`.

`core::DebugContext`: runtime root. Owns `LldbProvider`, `EventBus`, 7
components: BreakpointManager, MemoryManager, DisassemblyManager,
ModuleManager, DataManager, ExecutionController, TargetManager.

## Capabilities (hardware-verified: STM32 Nucleo H7S3L8 + OpenOCD)

- initialize / attach / configurationDone handshake
- breakpoints: source, function, instruction, data, exception
- execution control: continue, pause, next, stepIn/Out, stepInTargets
- stackTrace, threads, scopes, variables, setVariable, evaluate, completions
- memory read/write
- disassemble, source (disassembly fallback)
- modules, moduleSymbols, compileUnits, locations, exceptionInfo
- restart, disconnect, terminated event (LLDB statistics dump)
- async stopped/continued/exited/module/breakpoint events, SB event thread
- attach-only launch: extension rewrites `launch` -> `attach` against
  OpenOCD. Engine's own `launch` handler: LLDB-native local-process spawn,
  unused for embedded targets.

Tests: `modules/dap/test/handshake_test.py` (protocol only, no hardware),
`modules/dap/test/attach_smoke_test.py` (hardware).

## Trace pipeline (`trailer`)

OpenCSD-based, disjoint from `trailer-dap` (zero cross-references). Phase 1
(source correlation) done. Phase 2+ (deterministic replay, profiling,
coverage): not started. Detail: `TRACE_ANALYSIS_IMPLEMENTATION_PLAN.md`.

## Pain points

1. **Locking.** `LldbProvider::GetAPIMutex()` public. `DebugContext`
   re-exposes it. Every `core` component hand-rolls
   `lldb::SBMutex lock = ...; std::lock_guard<...> guard(lock);` per method,
   no compiler enforcement. `BreakpointManager::SetSourceBreakpoints`: 2-arg
   overload locks, 3-arg overload assumes lock held - silent hazard on
   misuse. SB event-pump thread touches the same SBTarget/SBProcess/SBThread
   state concurrently with the request-dispatch thread: a real race, not a
   style issue. Fix: move serialization inside `LldbProvider`, remove
   `GetAPIMutex` from every layer above it, drop the overload split.

2. **DebugContext public surface.** God-object: provider, event bus, 7
   components, plus raw accessors (`Target()`, `GetLLDBThread`,
   `GetLLDBFrame`) and 5 `std::function` seams (`log_diagnostic_`,
   `client_name_`, `client_feature_enabled_`, `get_custom_capabilities_`,
   `request_stop_`) wired in `session.cc`. Any handler reaches any
   component's internals via `context_.X()`. No narrow interface between
   layers. Fix: split read-only queries from mutating operations, remove
   direct `Target()`/`GetLLDBFrame` access from handlers.

3. Cancellation: structural no-op. `cancelled_requests_` is only erased
   (`CancelRequestHandler`), never inserted - single-threaded
   read-then-dispatch loop leaves no in-flight request to mark cancelled.

4. Wire-body type safety: `protocol::Event::body` / `Response::body` are
   `std::optional<llvm::json::Value>` with implicit `toJSON`. No
   compile-time check the right body type reaches the right event/response.

5. Config scatter: `DebugContext` mirrors flags (`auto_variable_summaries_`,
   `command_escape_prefix_`) that `TargetManager` should own outright.

6. Inconsistent peer access: `MemoryManager`/`ModuleManager` hold
   `LldbProvider&` directly; every other component holds `DebugContext&`.

7. No `core` unit tests. Verification: integration only (handshake test +
   hardware smoke test).

8. `providers/disassembler` unused by `trailer-dap` - `disassemble` handler
   calls `target.ReadInstructions` directly instead.

9. `snapshot_parser`: linked by `trailer`, never `#include`d. Orphaned.

## Not started

- OpenOCD TCL RPC client (engine-side). Blocks Registers/Instruction-Trace
  views, `TraceService`.
- Real OpenOCD flash+reset `launch` (current `launch`: LLDB-native local
  spawn, unused).
- Locking redesign (pain point 1).
- DebugContext surface redesign (pain point 2).
- SVD/peripheral memory strategy (`MemoryManager` has one strategy today).
- Wiring `providers/disassembler` into the `disassemble` handler.
