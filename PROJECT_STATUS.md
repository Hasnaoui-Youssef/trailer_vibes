# Project Status

Debug engine: one executable, `trailer-dap`, LLDB-backed with an embedded
OpenOCD and a full CMSIS-SVD device/peripheral subsystem.

Full diagrams (layers, class relationships, request sequence, event
dataflow): `docs/architecture/`. This file: capabilities, verification, and
what's actually planned next.

## Build

CMake + Ninja + Clang only. LLVM/Clang/LLDB from system packages; Xerces-C
for CMSIS-SVD parsing. See `INSTALL.md` for the full prerequisite list.

```
cmake --preset linux
cmake --build build
```

Outputs `build/trailer-dap`, plus a `resources/` tree copied next to it
(device index + bundled architecture SVDs + CubeMX Rzone memory-map data).

## Layout

```
dap_protocol (wire types, LLDB-free)
  <- dap_core (transport, orchestrator, dispatch - reader thread + dispatch thread)
    <- dap_handlers (~48 request handlers)
      <- core (10 domain components)
        <- lldb_provider, openocd_provider, device_provider, disassembler, trace_provider
```

`core::DebugContext`: runtime root. Owns `LldbProvider`, an optional
in-process `OpenOcdProvider` (launch-mode only), `EventBus`, and 10
components - `BreakpointManager`, `MemoryManager`, `DisassemblyManager`,
`ModuleManager`, `DataManager`, `ExecutionController`, `TargetManager`,
`WatchManager`, `DeviceManager`, `TraceManager`.

## Capabilities (hardware-verified: STM32H7S3L8 Nucleo + OpenOCD)

**Standard DAP:**
- `initialize`/`launch`/`attach`/`configurationDone` handshake, real `cancel`
  support (a dedicated reader thread marks a request cancelled before it's
  even dispatched)
- Breakpoints: source, function, instruction, data, exception - with an
  automatic hardware-vs-software decision per breakpoint location, driven
  by the device's real memory map (RAM-resident locations get software
  breakpoints, conserving the limited hardware comparator count; ROM/flash
  locations get hardware breakpoints, since software breakpoints silently
  no-op against on-chip flash)
- Execution control: `continue`, `pause`, `next`, `stepIn`/`stepOut`,
  `stepInTargets`
- `stackTrace`, `threads`, `scopes`, `variables`, `setVariable`, `evaluate`,
  `completions`
- Memory `read`/`write`, two interchangeable backends (LLDB-process-backed,
  OpenOCD-backed)
- `disassemble`, `source` (disassembly fallback)
- `modules`, `moduleSymbols`, `compileUnits`, `locations`, `exceptionInfo`
- `restart`, `disconnect`, `terminated` event (LLDB statistics dump)
- Async `stopped`/`continued`/`exited`/`module`/`breakpoint` events off the
  LLDB SB event thread

**`launch` embeds OpenOCD in-process** - no external `openocd` process, no
subprocess spawn. This is what backs everything below that needs to keep
working regardless of halt state or that isn't representable over the GDB
Remote protocol at all. `attach` connects to an already-running external
gdbserver instead (owned by whatever launched it, e.g. the VS Code
extension) and never embeds OpenOCD - features listed as "launch-only"
below are unavailable under `attach`.

**Custom, trailer-specific:**
- `trailerWatchStart`/`Stop` - live memory polling, one push event per tick,
  keeps working while the target is running (reads through OpenOCD
  directly, bypassing LLDB's halt requirement). Launch-only.
- `trailerDeviceInfo`, `trailerPeripheralDetail`, `trailerPeripheralRead`,
  `trailerPeripheralWrite`, `trailerPeripheralWatchStart`/`Stop` - full
  CMSIS-SVD-driven peripheral register/field access, for both the
  user-supplied device SVD and the bundled architecture SVD (NVIC, SCB,
  DWT, ...) via a `core: true` argument. Coalesced safe-register-only reads
  by default; a register's own `readAction`/`access` metadata decides
  whether it's safe to read without side effects. Watches are launch-only
  for the same reason memory watches are; one-shot read/write work under
  either mode.
- `trailerTraceEnable`/`Disable`/`Status` - live ETMv4 instruction trace,
  captured through OpenOCD's TMC extraction and decoded (OpenCSD) on a
  dedicated single-consumer worker thread, pushed as `trailerTraceData`
  events with source correlation. Launch-only.

**Device/peripheral truth comes from the STM32CubeMX database**, not a
CMSIS DFP/PDSC - richer memory-region data (RAM/Flash/external regions with
explicit types) than the PDSC provides for the same parts. See
`docs/architecture/01-modules-and-layers.md`'s `device_provider`/`device_xml`
entries and the plan history for why.

## Tests

- `modules/dap/test/handshake_test.py` - protocol only, no hardware.
- `modules/dap/test/attach_smoke_test.py`, `launch_smoke_test.py`,
  `stepping_smoke_test.py`, `peripheral_smoke_test.py` - hardware, DAP-level.
- `modules/core/test/event_bus_test.cc`,
  `modules/dap/test/orchestrator_cancel_test.cc` - concurrency-focused unit
  tests (multi-thread `Publish()` stress, cancel-before-dispatch), no
  hardware.
- `modules/providers/device_provider/test/{device_provider_test,device_svd_test}.cc`,
  `modules/providers/disassembler/test/*.cc`,
  `modules/providers/trace/*/test/*.cc` - provider-level sanity checks, no
  hardware.
- `modules/providers/openocd_provider/test/openocd_provider_hw_test.cc` -
  hardware.
- The engine has also been run clean under ThreadSanitizer against real
  hardware in `launch` mode (a separate `build-tsan/` preset), including the
  memory-watch and peripheral-watch worker threads.

## Known characteristics (not blockers, worth knowing)

See `docs/architecture/05-known-characteristics.md` for the current,
verified list - two `DebugContext` accessors and the handlers built on them
bypass the `WithTarget` locking model, `MemoryManager`/`ModuleManager` hold
`LldbProvider&` instead of `DebugContext&`, a small dead `Service`
abstraction remains, wire event/response bodies are untyped
`llvm::json::Value`, and a couple of config values are split across
`DebugContext`/`TargetManager` ownership. None of these are active bugs;
none block the roadmap below.

## What's next

Everything that was previously tracked here as "not started" - the OpenOCD
TCL RPC client, real flash+reset `launch`, the SVD/peripheral memory
strategy, `core`-level unit tests, wiring `providers/disassembler` into the
`disassemble` handler - is done. The two real remaining axes of work are:

**Packaging.** Neither the engine nor the VS Code extension is packaged for
distribution yet - today, "using this" means building from source. Needed:
a Windows installer for the engine (bundling `trailer-dap`, its `resources/`
tree, and whatever LLVM/LLDB/OpenOCD runtime pieces aren't already present
on a target machine), and packaging the `trailer` VS Code extension itself
(VSIX build, marketplace or private-distribution publish) so the engine
binary ships with it rather than requiring a separate manual build step.

**Expanding trace features.** The instruction-trace pipeline is live and
hardware-verified end to end (ETMv4 capture -> OpenCSD decode -> source
correlation -> DAP push events -> the extension's trace views), but only
covers the first slice of what `CLAUDE.md`'s "Instruction Trace" and
"Profiling" sections describe as the long-term goal. Not yet started:
timeline navigation and exception visualization in the decoded stream,
runtime profiling (flame graphs, call graphs, function statistics), code
coverage, and ETMv3 support (only ETMv4 has been exercised against real
hardware so far).
