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

## Not yet implemented

### Session wiring

- Constructing an OpenOCD provider as part of a debug session, and selecting the memory/trace
  backend for that session, is not yet wired up.

### Trace configuration

- Creating a trace sink or trace source at runtime is not implemented.
  Trace components currently must already exist, created ahead of time through startup configuration.

### Device memory region information

- Extracting a device's memory layout (flash and RAM regions, with their address ranges) from a
  device family pack description is not implemented.
- Extracting a device's peripheral register map (names, addresses, and address ranges) from a
  peripheral description file is not implemented.
- Extracting debug component topology from a debug description file, where one is available, is not implemented.
  This description is not always present for a given device and must be treated as optional.
- Extracting stack and heap region bounds from a linker script is not implemented.
- A unified memory region model — named regions (flash, RAM, peripherals, stack, heap, and others)
  with address bounds, populated from the sources above — does not exist yet.
- Modifying a memory region's bounds, or adding/removing a region, after it has been populated
  from the sources above, is not implemented.

### Region- and AP-scoped memory view

- Selecting a memory access target by AP number alone is not implemented. Today, addressing a
  specific AP requires naming an OpenOCD target that was already configured to sit on that AP;
  there is no functionality to address an AP directly without such a target pre-existing.

### Trace view

- Decoding a captured trace buffer into an ordered sequence of executed instructions is not wired
  to live hardware-captured data; the trace decode pipeline exists independently but is not fed by
  the provider's trace extraction.
- Exposing a queryable, ordered trace timeline (instruction address, disassembly, and source
  correlation per entry) is not implemented.
