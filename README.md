# trailer-dap

An embedded-systems debugging engine: a single binary, `trailer-dap`,
that speaks the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
over stdio and implements it against real hardware over SWD/JTAG. It's the
backend for the `trailer` VS Code extension (`../trailer/`), but it isn't
VS Code-specific - the protocol boundary is the only thing above it.

Built on:

- **[LLDB](https://lldb.llvm.org/)**, via its SB API - symbols, registers,
  memory, variables, expression evaluation, breakpoints, stepping,
  disassembly.
- **[OpenOCD](https://openocd.org/)**, linked in-process (not spawned as a
  subprocess) - flashing, live memory/peripheral polling while the target
  runs, and instruction-trace (ETM) capture, none of which fit through
  LLDB's GDB Remote connection to OpenOCD.
- **CMSIS-SVD**, parsed via a CodeSynthesis XSD-generated C++ binding -
  per-device peripheral register/field maps.
- **STM32CubeMX's device database** (not a CMSIS DFP/PDSC) - per-device
  memory maps, used to decide hardware-vs-software breakpoints and to
  resolve the bundled architecture SVD for a given part.
- **[OpenCSD](https://github.com/Linaro/OpenCSD)** - ETMv4 instruction
  trace decode.

## Building

```
cmake --preset linux
cmake --build build
```

See `INSTALL.md` for prerequisites (LLVM/Clang/LLDB dev packages, Xerces-C)
and the Windows (MSYS2) preset. Output: `build/trailer-dap`, plus a
`resources/` tree copied next to it (device index and bundled SVDs).

## What it can do

Standard DAP - breakpoints (with an automatic hardware-vs-software decision
per location, driven by the device's real memory map), execution control,
stack/variable inspection, expression evaluation, memory read/write,
disassembly, module/symbol queries - plus a custom surface for things no
generic DAP client models:

- **Live memory watches** (`trailerWatchStart`/`Stop`) - push updates while
  the target keeps running, reading straight through OpenOCD.
- **Peripheral access** (`trailerDeviceInfo`, `trailerPeripheralDetail`,
  `trailerPeripheralRead`/`Write`, `trailerPeripheralWatchStart`/`Stop`) -
  full CMSIS-SVD register/field access for both the target device and the
  bundled Cortex-M architecture peripherals (NVIC, SCB, DWT, ...).
- **Live instruction trace** (`trailerTraceEnable`/`Disable`/`Status`) -
  ETMv4 capture through OpenOCD, decoded live, pushed with source
  correlation.

All of the above (except one-shot memory/peripheral read/write) requires
`launch` mode, where the engine owns OpenOCD in-process; `attach` connects
to an already-running external gdbserver instead and doesn't get them.

See `PROJECT_STATUS.md` for the full capability list, test coverage, and
what's actually planned next. See `docs/architecture/` for how it's put
together - start at `docs/architecture/00-overview.md`.

## Repo layout

```
modules/
  dap/                  DAP wire types, transport/dispatch, request handlers
  core/                 DebugContext + 10 domain components (the engine's brain)
  providers/
    lldb_provider/      Owns SBDebugger/SBTarget, the WithTarget locking seam
    openocd_provider/   Embeds OpenOCD in-process
    device_provider/    STM32 device/peripheral truth (CubeMX + CMSIS-SVD)
    disassembler/       LLVM-MC disassembly, independent of LLDB
    trace/              OpenCSD-based instruction trace decode pipeline
resources/               Bundled device index, architecture SVDs, CubeMX Rzone data
```

## Testing

`modules/dap/test/*.py` are DAP-level smoke tests over real hardware (an
STM32H7S3L8 Nucleo); `handshake_test.py` is the one that needs no hardware.
Provider- and component-level sanity checks live alongside their code as
plain `int main()` executables (`modules/*/test/*.cc`), including a couple
of concurrency-focused unit tests (`event_bus_test`,
`orchestrator_cancel_test`) with no hardware dependency. The engine has also
been run clean under ThreadSanitizer against real hardware. Full list in
`PROJECT_STATUS.md`.
