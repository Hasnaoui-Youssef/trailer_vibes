# DAP engine architecture: overview

Scope: `trailer-dap`, the DAP adapter binary (`src/dap_main.cc:23-49`). Not
openocd, not the trailer VS Code extension. The trace/OpenCSD pipeline
(`trailer` binary, `modules/providers/trace/*`) is a disjoint sibling - see
the note at the end of `05-limitations.md`.

## Two executables, one repo

`engine/CMakeLists.txt` builds two executables from the same module set:

- `trailer` (`CMakeLists.txt:68-82`) - the trace-analysis CLI. Links
  `OpenCSD::opencsd`, `snapshot_parser`, `trace_sink`, `trace_decoder`,
  `trace_model`, `disassembler`, `trace_transform`.
- `trailer-dap` (`CMakeLists.txt:91-102`) - the DAP adapter. Links `dap_core`,
  `debug_service`, `dap_handlers`.

They share only `common` and `modules/providers` at the CMake level
(`add_subdirectory` calls, `CMakeLists.txt:63-66`). No other coupling.

## Layering thesis

Five targets sit under `trailer-dap`, top to bottom:

```
dap_handlers  (concrete request handlers, grouped by domain)
dap_core      (Transport, Orchestrator, generic dispatch seam)
dap_protocol  (wire-format value types + JSON conversion)

core          (DebugContext + 7 domain components)
lldb_provider (SBDebugger/SBTarget ownership, header-only)
```

`dap_handlers` links both the `dap_core`/`dap_protocol` line and the
`core`/`lldb_provider` line - it is the seam where communication meets
domain. Full link graph and target-to-directory mapping in
`01-modules-and-layers.md`.

## Two hard rules (as documented in source)

1. **LLDB boundary**: only `core` and `lldb_provider` may name an `lldb::`
   type. Stated explicitly in `debug_context.hpp:193-196` ("no layer above
   core may name an lldb:: type... no lock is ever exposed above core") and
   in `request_handler.hpp:38-41` ("no layer above core/providers may know
   about the LLDB SB API"). Verified NOT held in practice for ~15 handlers -
   see `05-limitations.md` #1-2.
2. **JSON boundary**: `core` never builds a `llvm::json::Value` or a
   `dap::protocol::Message`; every `DomainEvent` alternative is plain data or
   an already-typed `protocol::*EventBody` (`event_bus.hpp:1-13`). Only
   `dap_handlers`' event translator (`session.cc`, the sole `EventBus`
   subscriber) calls `toJSON`. Held consistently - no violation found.

stdout carries only DAP frames; diagnostics go to stderr
(`src/dap_main.cc:1-6`), matching the `trailer` CLI's same rule.

## Provenance

`trailer-dap` forked LLVM's `lldb-dap` and is mid-migration: domain logic is
being carved out of the fork's `DebugService` god-class into `core`'s
`DebugContext` + 7 managers, one component at a time (see
`debug_context.hpp:1-22`'s class comment, `PROJECT_STATUS.md`). The fork
remnant (`modules/dap/src/debug_service/`) is still built and linked; several
handlers still depend on it. This half-migrated state is the source of most
findings in `05-limitations.md`.

## Document index

- `01-modules-and-layers.md` - CMake link DAG, target-to-directory map.
- `02-class-relationships.md` - `DebugContext`, managers, handler base
  classes.
- `03-request-sequence.md` - one request, wire to wire.
- `04-event-dataflow.md` - LLDB event thread to DAP event.
- `05-limitations.md` - ranked, honest review of every structural flaw found.
