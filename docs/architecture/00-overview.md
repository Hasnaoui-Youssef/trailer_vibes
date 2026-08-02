# DAP engine architecture: overview

Scope: `trailer-dap`, the single executable this repo builds
(`src/dap_main.cc`). There is no longer a separate trace-analysis CLI - the
instruction-trace pipeline is a linked-in component of the same binary (see
`01-modules-and-layers.md`).

## One executable

`engine/CMakeLists.txt` builds exactly one target, `trailer-dap`, which links
`dap_core` and `dap_handlers`. Everything else - the domain layer, LLDB,
OpenOCD, the device/peripheral subsystem, instruction-trace decoding - is
pulled in transitively through `core`.

## Layering

```
dap_handlers  (concrete request handlers, grouped by domain)
dap_core      (Transport, Orchestrator, generic dispatch seam)
dap_protocol  (wire-format value types + JSON conversion)

core          (DebugContext + 10 domain components)
lldb_provider, openocd_provider, device_provider, disassembler, trace_provider
```

`dap_handlers` is the seam where communication meets domain: it links both
the `dap_core`/`dap_protocol` line and `core`. Full link graph and
target-to-directory mapping in `01-modules-and-layers.md`.

## Two boundary rules

1. **JSON boundary, held with no exceptions.** `core` never builds a
   `llvm::json::Value` or a `dap::protocol::Message`; every `DomainEvent`
   alternative is plain data or an already-typed `protocol::*EventBody`
   (`core/include/core/event_bus.hpp`). Only `dap_handlers`' event
   translator (`session.cc`, the sole `EventBus` subscriber) calls `toJSON`.
2. **LLDB boundary, held at the module level, not fully at the handler
   level.** Only `core`, `lldb_provider`, `openocd_provider`, and
   `device_provider` may link LLDB/OpenOCD/Xerces-C directly - `dap_protocol`
   and `dap_core` are genuinely free of both. Within `dap_handlers`, however,
   a number of handlers (`variables`, `evaluate`, `scopes`, `stackTrace`,
   `threads`, most of `symbols/*`) do drive `lldb::SB*` types directly
   instead of only calling `core` methods - `dap_handlers` is documented as
   "the only layer allowed to know about a specific component," and in
   practice that license is used down to the SB API in places, not just at
   the manager-method level. This is a real, current characteristic, not a
   violation waiting to be caught - see `05-known-characteristics.md`.

stdout carries only DAP frames; diagnostics go to stderr (`src/dap_main.cc`).

## What changed since the last architecture pass

This document set was rewritten from scratch rather than patched, because
the previous version described a mid-migration state that no longer exists:

- The `trailer` trace-analysis CLI and the `debug_service` fork remnant (a
  half-absorbed port of `lldb-dap`'s `DebugService` god-class) are both
  gone. The migration they were mid-way through - carving domain logic out
  of the fork into `core`'s `DebugContext` + components - is complete.
- `core` grew from 7 components to 10: `TraceManager` and `WatchManager`
  (engine-owned live memory polling) and `DeviceManager` (CMSIS-SVD
  peripheral modeling) were added.
- The manual `GetAPIMutex()`/`lock_guard` idiom that used to be repeated
  per-method across ~25 call sites was replaced by
  `LldbProvider::WithTarget(callable)` - a single encapsulated critical
  section, not a lock object callers can forget to take or misplace.
- Cancellation went from a structural no-op to real: the request reader now
  runs on its own thread and marks a request cancelled before it is even
  dispatched.
- OpenOCD is no longer a stub - it's linked in-process
  (`openocd_provider`), embedded by `TargetManager::Launch`, and backs
  memory reads/writes, live memory/peripheral watches, and instruction
  trace capture.
- The device/peripheral subsystem (`device_provider`, `DeviceManager`, the
  `trailerDeviceInfo`/`trailerPeripheral*` DAP surface) didn't exist at all
  in the previous pass.

## Document index

- `01-modules-and-layers.md` - CMake link DAG, target-to-directory map.
- `02-class-relationships.md` - `DebugContext`, its 10 components, handler
  base classes.
- `03-request-sequence.md` - one request, wire to wire; the reader/dispatch
  thread split.
- `04-event-dataflow.md` - every thread that can push a `DomainEvent`, LLDB
  event thread to OpenOCD-backed watch/trace workers, to DAP event.
- `05-known-characteristics.md` - honest, current, short list of real
  architectural characteristics worth knowing before extending the engine.
  Not a backlog - see `../../PROJECT_STATUS.md` for that.
