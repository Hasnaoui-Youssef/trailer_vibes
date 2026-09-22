# AGENT.md

# Trace Provider - Trailer

## Project Overview

This provider implements a high-performance instruction trace analysis engine for Arm CoreSight trace data.

The provider reconstructs program execution from Embedded Trace Macrocell (ETMv3/ETMv4) instruction trace and exposes the decoded information through machine-readable interfaces for consumption by external debugger frontends.

This component is **not** intended to be a standalone application or interactive debugger.

Its sole responsibility is deterministic processing of trace data.

External applications are responsible for:

- Trace acquisition
- Debug probe communication
- ETM configuration
- Process lifecycle
- User interface
- Session management

This component only consumes the collected data and produces structured analysis.

---

# Design Philosophy

The component favors:

- deterministic behavior
- simple control flow
- explicit ownership
- low runtime overhead
- composable modules
- minimal abstractions

Prefer code that is obvious over code that is clever.

Avoid unnecessary abstraction layers.

Avoid introducing frameworks.

If a problem can be solved with ordinary functions and value types, do not introduce inheritance.

---

# Scope

The engine is responsible for:

- consuming firmware images
- consuming trace data
- configuring OpenCSD decoders
- reconstructing executed instructions
- correlating instructions with DWARF information
- generating higher level execution information
- exposing results to external elements

The engine is **not** responsible for:

- loading firmware images
- extracting trace data
- flashing targets
- configuring OpenOCD
- interacting with GDB
- communicating with debug probes
- implementing IDE functionality
- graphical interfaces

---

# Primary Libraries

The provider intentionally relies on existing libraries instead of reimplementing functionality.

## LLVM

LLVM is used for:

- object file loading
- ELF parsing
- DWARF parsing
- instruction disassembly
- symbol lookup
- source correlation

Do not replace LLVM functionality with custom implementations.

---

## OpenCSD

OpenCSD is the authoritative decoder for CoreSight trace protocols.

Its responsibility ends at generating generic trace elements.

This project builds higher-level analysis on top of those decoded elements.

Do not duplicate protocol decoding logic that already exists in OpenCSD.

---

# Supported Inputs

The trace engine should eventually support multiple input formats.

Possible formats include:

## Structured Configuration

External elements may provide:

- firmware image path
- trace data path
- ETM register values
- decoder configuration

The decoding pipeline should remain independent of the transport mechanism.

---

# Architecture

The project is organized as a processing pipeline.

```
Input
    │
    ▼
Configuration Loading
    │
    ▼
Trace Source
    │
    ▼
OpenCSD Decoder
    │
    ▼
Generic Trace Elements
    │
    ▼
Instruction Reconstruction
    │
    ▼
Execution Analysis
    │
    ▼
Profiling / Timing / Statistics
    │
    ▼
Serializable Results
```

Each stage should expose a well-defined interface.

Avoid coupling unrelated stages.

---

# Communication

This project should be embeddable inside larger debugging systems.

The analysis engine should not depend on a specific communication backend.

---

# Data Ownership

Favor value semantics.

Avoid global state.

Avoid hidden ownership.

Prefer:

- std::unique_ptr
- std::optional
- std::variant
- std::span
- std::string_view

Avoid shared ownership unless ownership is genuinely shared.

---

# Error Handling

Recoverable errors should be propagated explicitly.

Fatal programming errors should use assertions.

Do not silently ignore malformed trace data.

Error messages should provide enough context for external tools.

---

# Performance

Instruction traces may contain millions of packets.

Code should avoid:

- unnecessary heap allocations
- repeated string copies
- unnecessary virtual dispatch
- hidden O(N²) algorithms

Prefer streaming algorithms whenever possible.

Avoid premature optimization.

Measure before optimizing.

---

# C++ Guidelines

Language standard:

- C++23

Compiler:

- Clang

Build system:

- CMake, always configured with the Ninja generator (`cmake -S . -B build -G Ninja`) and Clang (`CC=clang CMAKE_C_COMPILER=clang`, `CXX=clang++ CMAKE_CXX_COMPILER=clang++`). Never configure or build this project with another generator (e.g. Unix Makefiles) or another compiler (e.g. GCC).

Follow the Google C++ Style Guide.

---

## File Naming

Headers:

```
foo_bar.hpp
```

Sources:

```
foo_bar.cc
```

Tests:

```
foo_bar_test.cc
```

---

## Naming

Types:

```
TraceAnalyzer
```

Functions:

```
DecodeTrace()
```

Variables:

```
instruction_count
```

Constants:

```
kMaximumPacketSize
```

Namespaces:

```
trace
```

---

# Object-Oriented Programming

Use classes where they model ownership or encapsulate state.

Avoid inheritance unless there is a compelling architectural reason.

Avoid deep inheritance hierarchies.

Avoid runtime polymorphism unless required.

Prefer:

- composition
- free functions
- templates
- value types

Virtual functions should be rare.

---

# Memory Management

RAII is mandatory.

Never expose raw owning pointers.

Raw pointers should represent non-owning references only.

Prefer stack allocation whenever practical.

---

# Serialization

Analysis results should remain serializable.

Internal data structures should avoid transport-specific concerns.

Serialization formats should be implemented by adapters around the core data model.

---

# Testing

Every decoder stage should have reproducible test vectors.

Favor deterministic tests over randomized tests.

Tests should not require physical hardware.

---

# Future Work

Future functionality may include:

- function profiling
- execution timeline reconstruction
- exception visualization
- branch statistics
- cache analysis
- execution heat maps
- trace compression
- additional CoreSight component support

The architecture should remain flexible enough to support these additions without requiring major redesign.

---

# General Principles

When contributing code:

- Keep modules small.
- Prefer composition over inheritance.
- Minimize dependencies.
- Avoid global state.
- Avoid hidden side effects.
- Make ownership explicit.
- Keep interfaces narrow.
- Write deterministic code.
- Prefer readability over cleverness.
- Reuse LLVM and OpenCSD whenever possible.

# Architectural Rules

These rules should not be violated without explicit justification.

1. OpenCSD is the only component responsible for protocol decoding.

2. LLVM is the only component responsible for object file parsing and
   disassembly (and dissassembly isn't our concern).

3. Public code must never depend on analysis code internals.

4. Every stage of the pipeline should accept typed inputs and produce typed
   outputs.

5. Public interfaces should remain stable even if internal implementations
   change.

6. Prefer adding a new pipeline stage over modifying unrelated stages.

7. New features should be implemented by extending the pipeline, not by adding
   special cases throughout the codebase.

8. No component should own more than one responsibility.

9. The trace provider should remain usable as a library without modification.
