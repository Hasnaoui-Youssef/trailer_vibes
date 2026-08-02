# OpenOCD Integration

Status of the OpenOCD provider: what the engine can do today, and what functionality
is still needed to support the intended debugging experience.

## Functional today

### Lifecycle

- OpenOCD is linked into the engine process as a library (`modules/providers/openocd_provider`),
  not spawned as a subprocess. There is no OpenOCD entry point in the binary; the provider builds
  up and tears down the command context, adapter, transport, and servers itself.
- One provider instance per process. Construction and destruction are explicit and RAII-managed;
  destruction runs OpenOCD's shutdown sequence (including any registered pre-shutdown hooks)
  before releasing adapter and server resources.
- OpenOCD's own command dispatch loop runs on a dedicated thread. All engine-initiated OpenOCD
  operations are serialized onto that thread and return their result synchronously to the caller.
- A fatal internal OpenOCD error is contained to the provider rather than terminating the engine
  silently; it currently still ends the process, deliberately, as a simplification.
- OpenOCD's log output is redirected to a configurable file instead of the process's own
  stdio.

### Configuration

- Script search directories, config files, arbitrary raw startup commands, log file path, debug
  verbosity, and the GDB/Tcl/telnet server ports are all configurable per session.
- This configuration surface exists on the session's launch/attach parameters and is parsed.

### Memory access

- Reading and writing target memory through OpenOCD, addressed either through the debug session's
  current target or through an explicitly named target/AP, is implemented and available as a
  memory access backend alongside the existing LLDB-based backend.
- A memory read or write naming a target/AP that has no meaning for the currently selected
  backend is rejected with an error

### Trace

- Enumerating already-configured trace sink (TMC) and trace source (ETMv4) components, including
  their address, AP, and current state, is implemented.
- Enabling, disabling, and reconfiguring an already-configured trace sink or source is implemented.
- Extracting a trace sink's captured buffer as raw bytes is implemented.
- Trace sinks and sources must already exist (created ahead of time through startup configuration)

### Verified

- The full engine builds cleanly with the provider linked in.
- Provider construction and teardown, including shutdown hooks, has been verified without
  hardware attached.
- Against real hardware (STM32H7S3 over ST-Link): adapter connection,
  target examination, halting, and memory reads through the provider all work correctly.
- Reading the same memory address through two different APs provides different memory views.

- A memory read performed by the engine's OpenOCD-backed path and the identical read performed by
  a separate debugger are coherent

## Since resolved

The following were tracked here as not-yet-implemented and are now done -
kept as a record, not repeated as open items below:

- **Session wiring.** `TargetManager::Launch` constructs the OpenOCD
  provider (`DebugContext::CreateOpenOcd`) and installs an
  `OpenOcdMemoryStrategy` as the session's memory backend. `attach` never
  does this - see `docs/architecture/03-request-sequence.md`.
- **Device memory region information.** `device_provider` resolves a
  device's memory layout (RAM/Flash/external regions, with explicit
  RAM-vs-ROM typing) from the STM32CubeMX database, not a DFP/PDSC - see
  `docs/architecture/01-modules-and-layers.md`.
- **Device peripheral register map.** `device_provider`/`device_xml` parse
  CMSIS-SVD (both a user-supplied device SVD and the bundled architecture
  SVD) into names, addresses, fields, enumerated values, and aggregated
  read-safety per register - exposed via `core::DeviceManager` and the
  `trailerPeripheral*`/`trailerDeviceInfo` DAP requests.
- **Trace view fed by live hardware capture.** `TraceManager` subscribes to
  the provider's TMC extraction and decodes on its own worker thread; this
  is the same live pipeline `trailerTraceEnable`/`trailerTraceData` expose,
  hardware-verified end to end.

## Not yet implemented

### Trace configuration

- Creating a trace sink or trace source at runtime is not implemented.
  `TraceManager::Create` lists already-configured sinks/sources and uses the
  first of each; trace components must already exist, created ahead of time
  through startup configuration (a sourced OpenOCD config script).

### Debug component topology and linker-derived regions

- Extracting debug component topology (CoreSight topology - funnels,
  replicators, etc.) from a debug description file is not implemented. Not
  always present for a given device, so this must stay optional.
- Extracting stack and heap region bounds from a linker script is not
  implemented - `device_provider`'s `MemoryMap` covers RAM/Flash/external
  regions from the CubeMX database, not stack/heap.
- Modifying a memory region's bounds, or adding/removing a region, after
  it's been populated, is not implemented - `MemoryMap` is read-only once
  built for a session.

### Region- and AP-scoped memory view

- Selecting a memory access target by AP number alone is not implemented.
  Addressing a specific AP still requires naming an OpenOCD target already
  configured to sit on that AP.
