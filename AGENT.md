# AGENT.md

# Embedded Debugger Extension

## Project Vision

This project aims to build a modern, full-featured embedded systems debugging tool.

The objective is **not** to build another thin wrapper around an existing debugger, but rather to develop a reusable debugging engine capable of powering multiple frontends.

Long-term, the engine should be portable to other IDEs, desktop applications, command line tools, or custom frontends with minimal effort.

---

# Functionality

The engine must provide the following functionalities as first class citizens.

This includes, but is not limited to:

- Debug session management
- Symbol handling
- Register management
- Breakpoints
- Memory access
- Variable inspection
- Expression evaluation
- Stack unwinding
- Peripheral modeling
- SVD parsing
- Instruction trace decoding
- Profiling
- Coverage analysis

---

## UI Agnostic

The engine must never depend on Visual Studio Code APIs.

It exposes a communication interface that any frontend can consume.

Possible future frontends include:

- Qt desktop application
- JetBrains plugin
- Standalone GUI
- Command line interface
- Web frontend

---

## Modular

Each subsystem should have a clearly defined responsibility.

Examples include:

- DAP layer
- Debug session manager
- Symbol manager
- Memory subsystem
- Register subsystem
- Breakpoint subsystem
- Trace subsystem
- SVD subsystem
- Profiling subsystem

Modules communicate through well-defined interfaces.

---

# High-Level Architecture

```

          Debug Adapter Protocol
                     │

──────────────── Communication ────────────────

                     │

                     │

                Transport

                     │

                Orchestrator

                     │

                 Handlers
 ┌─────────────────────────────────────────────┐
 │                                             │
 │ Debug Session Manager                       │
 │ Breakpoints                                 │
 │ Register                                    │
 │ Variable                                    │
 │ Memory                                      │
 │ Symbol                                      │
 │ Stack                                       │
 │ Peripheral                                  │
 │ SVD Parser                                  │
 │ Instruction Trace                           │
 │ Profiling                                   │
 │ Coverage                                    │
 │ Runtime Analysis                            │
 │                                             │
 └─────────────────────────────────────────────┘

            │                     │

            │                     │

      Orcestrator          Debug Context ──────────────────── Component Managers

            │                     │

            │                     │

            │                   Providers (Resource Ownership e.g. OpenOCD server/LLDB Debugger)
        DAP Client        ┌─────────────────────────┐
                          │  LLDB SB API            │
                          │  OpenOCD TCL Interface  │
                          │  Trace Engine           │
                          │  Disassembly            │
                          └─────────────────────────┘

                                     │

                                OpenOCD Server

                                     │

                                Target Device
```

---

# Debugging Backend

LLDB is responsible for:

- Executing the Debug Adapter operations
- Symbol loading
- Register access
- Memory access
- Variable inspection
- Expression evaluation
- Thread management
- Stack unwinding
- Breakpoint management

LLDB communicates with OpenOCD using the standard GDB Remote Serial Protocol (RSP).

The engine never communicates with RSP directly.

---

# OpenOCD Integration

OpenOCD serves two distinct purposes.

## Standard Debugging

LLDB communicates with OpenOCD using the GDB Remote Serial Protocol.

Execution control, stepping, memory access, registers, and breakpoints all flow through LLDB.

---

## Extended Features

Many embedded debugging capabilities are outside the scope of GDB Remote.

Examples include:

- ETM configuration
- TMC configuration
- CoreSight component discovery
- Trace buffer extraction
- Vendor-specific monitor commands
- Hardware diagnostics

For these capabilities, the engine communicates directly with OpenOCD through its TCL interface.

This allows the engine to access functionality that is not represented in LLDB without using the monitor command.

---

# Debug Adapter Layer

The engine implements its own Debug Adapter Protocol layer.

The implementation should follow the DAP specification while remaining independent of any specific IDE.

The implementation is heavily inspired by LLVM's **lldb-dap** project, which serves as the primary architectural reference.
Custom DAP requests may be introduced to expose embedded-specific functionality not covered by the standard protocol.

Examples include:

- Peripheral access
- Trace decoding
- Coverage information
- Runtime profiling
- ETM configuration
- CoreSight topology

---

# Planned Features

The engine will expose dedicated features for embedded debugging.

## Registers

- Core registers
- Floating-point registers
- Special registers
- Optional architecture-specific register banks

Support:

- Editing
- Search
- Grouping
- Filtering

---

## Disassembly

Features include:

- Mixed source/assembly
- Instruction bytes
- Breakpoint markers
- Symbol annotations

The disassembly implementation is independent from LLDB (use LLVM).

---

## Breakpoint Manager

Support:

- Software breakpoints
- Hardware breakpoints
- Conditional breakpoints
- Function breakpoints
- Instruction breakpoints
- Tracepoints

---

## Memory View

Support:

- Arbitrary addresses
- Multiple data widths
- ASCII view
- Hex view
- Signed/unsigned display
- Floating-point display

Future additions may include memory region awareness.

---

## Peripheral View

Peripheral descriptions are generated directly from CMSIS-SVD files.

Support:

- Peripheral hierarchy
- Register fields
- Enumerations
- Bitfield editing

We own all SVD parsing.

---

## Variables

Support:

- Local variables
- Globals
- Statics
- Watches
- Expression evaluation
- STL container visualization
- Pretty printers

---

## Instruction Trace

Trace support includes:

- Trace component configuration (TMC, ETMv4, funnels, etc...)
- Timestamp reconstruction
- Exception visualization
- Timeline navigation
- Instruction history

The trace engine is independent from LLDB.

---

## Stack View

Support:

- Stack frames
- Inlined functions
- Exception frames
- Tail-call handling
- Frame navigation

---

## Profiling

Long-term profiling goals include:

- Flame graphs
- Runtime call graph
- Execution timelines
- Code coverage
- Hot path visualization
- Function statistics

---

# External References

The following projects are considered primary references during development.

## LLVM

- LLDB
- LLDB SB API
- lldb-dap

## OpenCSD

- Trace Decoding

## Perf
- Runtime Analysis

These define the debugging backend and DAP architecture.

---

# Engineering Guidelines

- Favor correctness over premature optimization.
- Prefer composition over inheritance.
- Minimize global state.
- Every subsystem should have a single responsibility.
- Separate DAP logic from debugging logic.
- Avoid exposing LLDB implementation details outside the LLDB provider.
- Keep communication interfaces stable.
- Design for future extensibility.
- Prefer explicit interfaces over implicit coupling.
- Every new subsystem should have a clear ownership model.

