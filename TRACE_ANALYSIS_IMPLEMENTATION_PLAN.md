# Execution Analysis — Implementation Plan

This plan structures the analysis capabilities to be built on top of the
existing decode pipeline (`Input → Configuration Loading → Trace Source →
OpenCSD Decoder → Generic Trace Elements → Instruction Reconstruction →
Execution Analysis → Profiling/Timing/Statistics → Serializable Results`,
per `CLAUDE.md`).

Today the pipeline goes from "Generic Trace Elements" (`trace_sink`'s flat
`std::vector<TraceRecord>`) through Instruction Reconstruction and Source
Correlation (Phase 1, done - see below for the actual modules built).
Everything past that is new work, added as new pipeline stages rather than
special cases bolted onto existing ones (Architectural Rules 5, 7, 8).

Phases are ordered by priority, each building on the previous one's output:

| Phase | Name | Status |
|---|---|---|
| 1 | Source correlation (instruction → line/file/function, line & function blocks) | **done** |
| 2 | Deterministic replay + control-flow integrity | next up |
| 3 | MMIO input vectors (user-supplied peripheral value reconstruction) | planned |
| 4 | Multi-core causality | moonshot |

### Phase 1 as built

The two-stage split played out as designed, plus one addition the RTTI
boundary required: LLVM and OpenCSD can never coexist in one translation
unit, so *both* the disassembly stage and the DWARF stage ended up as
independent `-fno-rtti` LLVM-side modules, bridged back to OpenCSD-side
code only through the dependency-free `trace_model` types.

- **`trace_model/`** — header-only, zero-dependency vocabulary shared
  across the RTTI boundary: `InstructionSet`, `DecodedInstruction`,
  `LoadSegment`, `ReconstructedInstruction`, `InlineFrame`,
  `SourceLocation`, `LineBlock`, `FunctionBlock`.
- **`disassembler/`** (Instruction Reconstruction, LLVM side, `-fno-rtti`) —
  `disasm::ProgramDisassembler`. Loads the ELF once, extracts PT_LOAD
  segments, disassembles Thumb/ARM ranges into `DecodedInstruction`s with
  control-flow classification (call/return/indirect/conditional, resolved
  direct-branch targets). Includes a `LooksLikeReturn` heuristic for
  `bx lr` / `pop {...,pc}`, since LLVM's own `MCInstrAnalysis::isReturn()`
  only recognizes a CodeGen-only pseudo the disassembler never produces.
- **`instr_reconstruct/`** (OpenCSD side, default RTTI) —
  `reconstruct::Reconstruct`. Bridges `TraceRecord` instruction ranges to
  `ProgramDisassembler`, honoring `TraceRecord::last_instr_executed`.
- **`source_correlator/`** (Execution Analysis, LLVM side, `-fno-rtti`) —
  `correlate::SourceCorrelator`. Independently loads the same ELF and
  builds an LLVM `DWARFContext`; `Resolve()` gives the full inline-frame
  chain for one address, `Correlate()` walks a `ReconstructedInstruction`
  stream into chronological `FunctionBlock`s (each holding its own
  chronological `LineBlock`s). Line blocks split on the *full* inline
  chain, not just the innermost file:line (confirmed with the user - two
  different inlined call sites stay distinct even if their bodies land on
  the same line); function blocks are a merge over adjacent line blocks
  sharing an outermost (real, non-inlined) function, not a second,
  independent pass.
- **`main.cc`** wires all of this into a real driver and prints both views
  (per-instruction with resolved location, and the FunctionBlock/LineBlock
  summary) - verified against the project's actual STM32H7 capture, not
  just synthetic fixtures: correctly resolves real HAL source locations
  (`startup_stm32h7s3xx.s`, `system_stm32h7rsxx.c`), and the `.data`
  copy-loop's line blocks show the expected one-block-per-iteration
  pattern.

Both new LLVM-side modules have deterministic unit tests against small,
committed, purpose-built fixtures (`disassembler/test/fixture.elf` /
`.s`, `source_correlator/test/fixture.elf` / `.c` - the latter compiled
with forced inlining specifically to exercise the full-inline-chain case).

---

## Phase 1 — Source Correlation

### Goal

Resolve every executed instruction to its file/line/function, then re-group
the flat instruction stream into two coarser, complementary views:

- **Line blocks**: maximal contiguous runs of instructions mapping to the
  same source line.
- **Function blocks**: maximal contiguous runs of instructions belonging to
  the same function, itself a coarser grouping over line blocks.

This is effectively "instruction ranges again," but re-segmented at source
granularity instead of trace-packet granularity, so both a fine (line) and
coarse (function) view exist without recomputing from scratch — function
blocks are built by merging adjacent line blocks that share a function,
not by an independent pass.

### Why this needs two new pipeline stages, not one

A `TraceRecord{kind = kInstructionRange}` only gives a `[start_addr,
end_addr)` span and an instruction count — it does not enumerate individual
instruction addresses. A single range can still straddle multiple source
lines or even multiple (inlined) functions with no intervening trace event,
because OpenCSD only emits a new element on a control-flow-relevant
boundary, not on a line-table boundary. So line/function segmentation
requires actually walking instruction-by-instruction inside each range,
which requires disassembly, which is its own pipeline stage per the
architecture diagram ("Instruction Reconstruction" sits before "Execution
Analysis").

**Stage A — Instruction Reconstruction** (new module, e.g.
`modules/instr_reconstruct/`)

- Input: `std::vector<TraceRecord>` + the loaded program image (ELF, already
  at `PipelineConfig::program_path`).
- For each `kInstructionRange` record: read raw bytes at `start_addr` from
  the ELF's loaded segments, and disassemble forward with LLVM's MC layer
  (`MCDisassembler`), selecting ARM/Thumb/AArch64 decode mode from
  `TraceRecord::isa`, stepping by each decoded instruction's size until
  `end_addr` is reached.
- Output: an ordered, flat `DecodedInstruction` stream:

  ```
  struct DecodedInstruction {
      ocsd_vaddr_t address;
      uint8_t size;
      ocsd_isa isa;
      ocsd_instr_type type;
      ocsd_instr_subtype subtype;
      bool executed;          // false only for a range's last instruction
                               // when TraceRecord::last_instr_executed was false
      ocsd_trc_index_t index_sop;   // originating TraceRecord's trace index,
                                    // for traceability back to raw trace
  };
  ```
- Correctness detail worth locking in now: `TraceRecord::last_instr_executed
  == false` means the *last* instruction decoded in that range (typically a
  conditional branch/IT-block instruction) did not actually execute. It must
  either be dropped or explicitly marked non-executed here, otherwise every
  downstream consumer (coverage, line blocks, replay) will silently
  overcount.
- `kException` / `kExceptionReturn` / `kTraceOn` / `kNoSync` records pass
  through untouched (they're not instruction ranges) but remain interleaved
  in trace order — later stages need them for context (e.g. "this line block
  was interrupted here").

**Stage B — Source Correlation** (new module, e.g.
`modules/source_correlator/`)

- Input: `DecodedInstruction` stream + DWARF (LLVM `DWARFContext` built over
  the same ELF).
- Per-instruction resolution must account for inlining: a single address can
  legitimately belong to a chain of inlined frames. Use LLVM's inlining-aware
  line lookup and represent a location as an ordered frame chain, innermost
  first:

  ```
  struct InlineFrame { std::string function; std::string file; uint32_t line; uint32_t column; };
  struct SourceLocation { std::vector<InlineFrame> frames; };  // frames[0] = innermost
  ```

- **Line blocks**: walk the resolved stream, start a new block whenever the
  full frame chain (or just the innermost file:line — needs a decision, see
  below) differs from the previous instruction's:

  ```
  struct LineBlock {
      SourceLocation location;
      ocsd_vaddr_t start_addr, end_addr;
      uint32_t instr_count;
      ocsd_trc_index_t index_sop;   // first contributing trace record
  };
  ```

- **Function blocks**: walk the line-block sequence, start a new block
  whenever the outermost (non-inlined) enclosing function changes:

  ```
  struct FunctionBlock {
      std::string function_name;
      ocsd_vaddr_t entry_addr;
      std::vector<LineBlock> line_blocks;   // or index range into a shared table
  };
  ```

- Both outputs are **chronological sequences**, not deduplicated per-line/
  per-function summaries — a loop produces one `LineBlock` per iteration
  pass, not one block for the line overall. This preserves the information
  needed for later phases (timing per invocation, call-depth-aware
  analysis). A separate aggregate/summary view (hit counts, coverage) is a
  cheap projection over this sequence and can be deferred to whichever
  Profiling/Statistics stage needs it — no need to compute it twice.

### Open design questions

- **Inline granularity for line blocks**: split on innermost frame only, or
  on the full inline chain? Splitting on the full chain is more precise
  (distinguishes "inlined copy A of `min()`" from "inlined copy B") but
  produces more blocks. Recommend: split on full chain, since function
  blocks already collapse this back down for anyone who wants the coarser
  view.
- **Function identity for recursion**: a function block per invocation
  (recursive calls produce sibling blocks, not one merged block) — matches
  the "chronological, not summarized" principle above, but needs a call-depth
  or parent-link field if we want to reconstruct nesting later (useful input
  to Phase 2's dynamic call graph).
- Naming for the two new modules is a placeholder — adjust to match
  whatever naming convention feels most consistent with `trace_decoder` /
  `trace_sink` when this is scoped.

### Dependencies

None beyond what's already in the pipeline (`config_parser`'s
`program_path`, `trace_sink`'s `TraceRecord` vector). This is the foundation
phase 2–4 build on.

---

## Phase 2 — Deterministic Replay + Control-Flow Integrity

### Goal

Given the exact decoded instruction stream (Phase 1) and the firmware's
known initial state, symbolically/concretely re-execute the deterministic
portion of the program — not just control flow, but actual register and
memory *values* — and use that to (a) validate control flow (CFI) and (b)
expose exactly where the trace stops being reproducible from static
information alone.

### Why initial state is more tractable than it sounds

Two things de-risk this:

- **Flash/ROM contents are fully known** from the ELF's loadable segments —
  no guessing needed for `.text`/`.rodata`.
- **If the trace starts at reset**, `.bss` zero-init and `.data` copy-down
  are themselves instructions *in* the trace — replay computes them
  correctly as a side effect of executing the startup code, rather than
  needing them as an assumption. Core register reset values (SP from the
  vector table, PC from the reset vector) are architecturally defined, not
  guessed. So "unknown initial RAM" is largely a non-issue as long as replay
  starts at or before the first read of any given memory location — if the
  trace starts mid-flight, initial RAM/register state becomes a real unknown
  and should be modeled as tainted-from-start rather than assumed-zero.

### Core mechanism: taint-tracked concrete replay

Maintain a shadow register file and a sparse shadow memory map, both
tri-state per cell rather than a single value:

```
enum class ValueState { kKnown, kTainted, kUnknown };
struct ShadowValue { ValueState state; uint64_t value; /* meaningful iff kKnown */ };
```

- Execute each `DecodedInstruction` against a per-instruction semantic model
  (start with data-processing/move/arithmetic/logic/memory ops; defer FP/
  SIMD/atomics — flag unsupported instructions as producing `kUnknown`
  results rather than guessing).
- **Taint sources**: MMIO reads (address matched against the SVD-derived
  peripheral map — see Phase 1's future SVD integration) and exception entry
  (the interrupt firing is an externally-timed event, even though the ISR's
  own instructions are fully traced). Any value derived from a tainted input
  is `kTainted`, propagated forward through the shadow state.
- **CFI check, for free**: replay already computes the shadow PC/register
  values used by indirect branches (`BX`, `BLX reg`, return addresses). Where
  the shadow value is `kKnown`, compare it against what the real trace says
  happened next. Disagreement is either a taint boundary we mis-tracked
  (bug in the replay model) or a genuine control-flow anomaly (corrupted
  pointer, unexpected vtable/function-pointer target, wild jump) — worth
  surfacing either way. This directly reuses Phase 1's function blocks for
  the static/possible side of the comparison (static call graph from
  disassembly vs. observed dynamic call graph).
- **Region legitimacy**: cross-check every executed address against
  ELF-declared section bounds; execution from a non-code region is always
  worth flagging regardless of taint state.

### Open design questions

- How much of the ISA to model initially — recommend scoping to the
  instruction types/subtypes already surfaced on `TraceRecord`
  (`last_instr_type`/`last_instr_subtype` categories) and growing coverage
  incrementally, with `kUnknown` as the safe fallback for anything
  unmodeled.
- Whether taint propagation needs to be flow-sensitive per-bit (e.g. a
  masked/shifted tainted value might have some known bits) — probably not
  worth it initially; whole-value taint is simpler and likely sufficient for
  the CFI use case.
- Memory model granularity (byte vs. word shadow map) — likely word-granularity
  with byte sub-tracking only if it turns out to matter in practice.

### Dependencies

Phase 1 (`DecodedInstruction` stream, function blocks for the static call
graph side of CFI comparison).

---

## Phase 3 — MMIO Input Vectors

### Goal

Let the user supply concrete values for specific MMIO reads (e.g. "the 5th
read of `ADC1->DR` returns `0x1F4`") and use Phase 2's replay engine to
deterministically reconstruct everything downstream of those reads instead
of leaving them `kTainted`.

### The two modes this can operate in — needs an explicit choice

**Mode A — anchored validation (recommended starting point).** Replay runs
*alongside a real captured trace*. The trace itself is the ground truth for
every branch outcome, so a user-supplied MMIO vector never needs to
disambiguate control flow — it only needs to fill in *data* values that
would otherwise stay tainted. This has a useful side benefit: it turns into
a guess-and-check tool. If a hypothesized vector produces a computed branch
condition that contradicts what the real trace shows happened, the vector
is wrong — this can be used to *infer* plausible peripheral values by
searching for ones consistent with observed control flow, rather than only
accepting user input at face value.

**Mode B — unanchored simulation.** Replay runs with no real trace to pin
control flow to (e.g. "what would happen on a future run"), essentially
turning the replay engine into a standalone ISA interpreter/emulator seeded
by the firmware image and user-supplied MMIO vectors. Here a data-dependent
branch is a genuine fork with no ground truth: it needs either a
user-supplied constraint ("assume this stays below threshold X") to pick one
path, or the tool has to explicitly fork and explore both, tagging each
branch with the constraint that produced it (this is, at that point,
concolic/symbolic execution rather than replay).

Mode A is a direct, incremental extension of Phase 2 and should come first.
Mode B is substantially larger in scope (a real firmware emulator) and
should be treated as a separate, later decision rather than assumed as part
of this phase.

### Sketch (Mode A)

- Input vector keyed by peripheral+register (from SVD) or, once Phase 1
  exists, by source location ("the `adc_read()` call at `foo.c:42`") plus an
  occurrence index (1st/2nd/nth read at that site).
- Config extension needed: `PipelineConfig` gains something like a list of
  `{peripheral_register_or_address, occurrence_index, value}` entries —
  natural fit as a new field on the existing config model rather than a new
  transport.
- A **constraints list**, per the user's own framing, is still useful even
  in Mode A: not to pick branches (the trace already did), but to assert
  *expected* properties of the reconstructed values ("this variable should
  never exceed X") that get checked post-hoc across the whole run — the
  same "invariant checking without compiled-in asserts" idea from the
  earlier brainstorm, now with concrete peripheral values feeding it instead
  of only pure computation.

### Dependencies

Phase 2 (replay engine and taint model). Benefits from, but doesn't strictly
require, Phase 1's source correlation for the source-location-based keying
scheme.

---

## Phase 4 — Multi-Core Causality (moonshot)

### Goal

For multi-core SoCs with multiple trace sources, correlate cross-core shared-
memory interactions into a happens-before graph — true concurrent-access
detection across cores, not just ISR-vs-main-thread races on a single core.

### Why this is realistic later rather than now

`TraceRecord::trace_id` already carries the CoreSight trace ID of the
source, so `trace_sink` is already multi-source-aware at the data-model
level — nothing needs to change there. What's missing is everything this
depends on:

- Phase 1, to know *which* global/memory object a core is touching
  (symbol resolution) at each point in its stream.
- Phase 2, to actually compute the *addresses* touched (control flow alone
  doesn't tell you which memory cell a load/store hits — that needs the
  replay engine's address computation).
- A shared time base across cores' independently-captured traces (a
  correlation problem in its own right — likely a global timestamp element
  if the target's ETM configuration provides one).

No further design work planned here until 1–3 are in place; listed now only
to keep the end goal visible when making earlier design decisions (e.g. not
hard-coding single-trace-source assumptions into Phase 1/2 data structures).

---

## Cross-cutting notes

- Every new stage keeps `ocsd_trc_index_t index_sop` (or the originating
  `TraceRecord`'s index) threaded through its output, so any downstream
  result — a line block, a replayed register value, a CFI violation — can be
  traced back to the exact raw trace position that produced it.
- All new output types should stay serializable value types (Architectural
  Rule 5 / "Serialization" section of `CLAUDE.md`) independent of any
  transport, consistent with everything built so far.
- SVD ingestion (peripheral register maps) isn't scoped as its own phase
  above because it's not useful in isolation — it first pays off inside
  Phase 2/3's taint classification (recognizing MMIO addresses) and future
  peripheral-semantics work. Worth adding as a config input whenever Phase 2
  starts.
