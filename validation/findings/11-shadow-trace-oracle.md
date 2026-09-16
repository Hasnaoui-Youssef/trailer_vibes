# Finding 11 — Independent-oracle single-step shadow trace (Phase 9.2)

Status: **measured on real hardware, W1, 400-instruction shadow trace vs.
a clean hardware-trace capture, both from the same starting point. Now at
100.00% concordance (400/400 exact matches, zero classified divergences),
after the defect this comparison found was fixed in the engine — see
"Fixed" below.** Closes Phase 9.2 — the replacement for the report's
unavailable vendor-tool comparison, and per Finding 08's recap, the
single largest open item this campaign had left for FR5.

Raw evidence: `validation/captures/w1_baseline/shadow_trace/shadow_addresses.json`
(the 400-step shadow arm, unchanged throughout) and `hardware_trace.txt`
(the decoded hardware arm, from the same `etm_sweep_cti/baseline/capture.bin`
named below — regenerated a second time with the fixed
`trailer-trace-replay`, so it reflects the current engine, not the one
that produced the original 99.75% figure below).

## Method

Two genuinely independent arms, both starting from the identical point
(a breakpoint at `w1_baseline/Core/Src/main.c:19`, the first statement of
the workload's steady-state loop):

- **Shadow arm**: `next` requests at `granularity: "instruction"` over the
  DAP, 400 steps, reading the PC (`stackTrace`'s
  `instructionPointerReference`) after each one. This uses LLDB's own
  hardware/FPB-based single-stepping — it never invokes this project's
  disassembler to decide where to step, so it is independent of the code
  under test for execution *order*.
- **Hardware arm**: the clean, breakpoint-gated capture already collected
  for Finding 06 (`w1_baseline/etm_sweep_cti/baseline/capture.bin`,
  starting from the identical breakpoint), decoded with
  `trailer-trace-replay` and its per-instruction address sequence
  extracted from the text output.

## First result, and the investigation it demanded

A naive index-by-index comparison of the two address sequences gave
**4.00% concordance** — alarmingly low. Rather than either dismiss this as
noise or declare a pipeline bug on the spot, the actual divergence was
traced to source. The first mismatch: the shadow trace visits `0x80003de`
(a `bgt.n` loop-exit check, confirmed via `objdump` — a genuine
conditional branch, not an IT-block predicated instruction) between two
addresses the hardware trace reports as adjacent. A temporary debug print
directly on the decoded `TraceRecord` (removed after use) showed the
raw range: `start=0x80003dc end=0x80003e0 last_instr_executed=false`.

Traced to OpenCSD's own source (`decoder/source/trc_gen_elem.cpp:143`):
`last_instr_exec` is rendered as `"E "`/`"N "` — OpenCSD's own field is
named and documented after ARM's ETM architecture's own "atom" terminology
(Executed / Not-executed), where **a not-taken conditional branch is
architecturally classified as an "N" atom** — not a claim that the CPU
skipped fetching or evaluating the instruction, but ARM's own term of art
for "this waypoint's conditional outcome did not redirect control flow."
This project's own transform
(`reconstructed_instruction_transform.hpp`) correctly reads this field and
excludes the branch instruction from the "executed" list on that basis —
faithfully implementing what OpenCSD reports, not a bug in this project's
code.

## Fixed: not-taken conditional branches are no longer dropped from the instruction listing

This was corrected after being found here, not left as a documented-but-
live defect. `trace_session.cc` (the production path) and `trailer_trace_replay.cc`
(the offline CLI) both filtered the reconstructed-instruction stream down
to `executed == true` before returning or printing it; both now return
every instruction, taken or not. `ReconstructedInstruction::executed` is
unchanged — it still means exactly what OpenCSD's `last_instr_exec`
atom means (see below) — only the silent drop downstream is gone.

Verified two ways: `trace_provider_test`'s frozen oracle moved from 114
to 118 executed instructions on the same fixture (6 function blocks and 2
gaps unchanged — `FunctionBlock`'s own grouping already filtered
internally, so it needed no change), and `trailer-trace-replay`'s default
output now agrees with the committed `trailer_trace_decoder_output.log`'s
own tail line ("118 reconstructed instructions") for the first time —
that file's own instruction *listing* still only shows 114, a
pre-existing inconsistency in the retired CLI it was copied from
(Finding 01), not a new discrepancy this fix introduces. Spot-checked
two of the four newly-included instructions directly: both are
conditional branches whose target is annotated (`-> 0x...`) but whose
*next* printed instruction is the fall-through address, correctly
showing "evaluated, not taken" rather than a silent gap in the listing.

The gap/exception index scheme (`instruction_index`, used throughout
Findings 12/15) is unaffected — it was always computed independently
from raw `TraceRecord`s, never from this filtered list.

A third, independent check: re-running this very comparison (the same
400-step shadow trace, a freshly-decoded hardware trace) after the fix
moved the result from 99.75% to a clean 100.00% — see "Result after the
fix" below.

## Real, previously-undocumented finding: not-taken conditional branches are excluded from the reconstructed instruction listing

This is worth stating plainly and separately from the concordance number,
because it is genuinely new information about this project's own
pipeline, distinct from the earlier-closed IT-block question (which was
about a *predicated non-branch* instruction potentially being
over-reported as executed). Here the direction is the opposite: a real
conditional *branch* instruction — one that corresponds to actual source
code (`main.c:20`'s loop condition) and that a developer would reasonably
expect to see in an instruction listing — is silently absent from that
listing whenever it isn't taken, while the identical static instruction
*is* shown on the one pass where it is taken. A developer reading the
reconstructed trace to understand "what code ran" would not see the
loop's own condition check on non-exiting iterations, even though the CPU
genuinely fetched, decoded, and evaluated it every time. This is
consistent with ARM's own atom semantics and is not a correctness defect
in the sense of wrong data — but it is a real, user-visible limitation of
the instruction listing that was not written down anywhere in this
project before this comparison surfaced it.

## Result before the fix

Re-running the comparison with divergences classified (a not-taken
conditional branch in the shadow trace, confirmed by disassembly, whose
*next* shadow step matches the hardware trace's current position, counts
as "explained" rather than a raw mismatch):

| | count |
|---|---:|
| Shadow trace length | 400 steps |
| Exact matches | 337 |
| Explained divergences (not-taken conditional branch) | 62 |
| Unexplained divergences | 1 |
| **Concordance (matches + explained) / total** | **99.75%** |

The one "unexplained" entry was at the very last shadow-trace index (399)
— there is no step 400 to confirm the classifier's "does the *next* shadow
step match" rule, so it was a boundary artifact of stopping the shadow
trace at exactly 400 steps, not a genuine divergence. Every divergence in
this run was fully explained by the same, single mechanism above.

## Result after the fix

With the not-taken branches no longer dropped, the hardware trace needs
no classification step at all — it now records the identical sequence
the shadow trace does, directly:

| | count |
|---|---:|
| Shadow trace length | 400 steps |
| Exact matches | 400 |
| Explained divergences | 0 |
| Unexplained divergences | 0 |
| **Concordance** | **100.00%** |

This is a stronger result than the pre-fix 99.75%, not merely a
different one: the classification step (`shadow_trace_compare.py`'s
"is this a not-taken branch whose next step matches" rule) existed only
to compensate for the engine's own omission. Removing the omission
removes the need for the compensation — the independent oracle now
matches the hardware reconstruction exactly, with no interpretation
required.

## What this validates, and what it doesn't

This validates instruction-level *execution order* — the hardware trace's
reconstructed address sequence matches an independently-obtained,
disassembler-independent oracle exactly, with no accounting or
classification needed, on a short, interrupt-free, 400-instruction run
of W1. It does not validate mnemonic/operand-level decode correctness (already
covered separately by the existing disassembler unit tests), and the
oracle is honestly this project's own LLDB-driven single-stepping, not a
third-party vendor tool — reported as such, not dressed up as external
validation.

## Caveats, stated honestly

- **N=400 instructions, one workload (W1).** W1 was chosen because it is
  short, deterministic, and interrupt-free by design — exactly the
  precondition this method needs. Extending to a longer run or a
  different workload (particularly one with real interrupts, which would
  test whether exception entry/exit produces its own classification needs)
  was not done here.
- **Single-stepping's own cost is real and was measured separately**
  (Finding 09: ~85ms median per step, dominated by OpenOCD's polling
  interval) — 400 steps took on the order of tens of seconds of wall
  time, which is why this comparison is scoped to hundreds, not thousands,
  of instructions.
- **The underlying omission is fixed (see above); a dedicated visual
  treatment is not.** The instruction is no longer missing from the
  listing, but nothing yet distinguishes "taken" from "not taken" at a
  glance beyond the existing branch-target annotation — a developer has
  to notice the next line is the fall-through address, not the annotated
  target. Whether to add an explicit visual flag is a UI design question,
  and building any profiling/coverage feature on top of this data is
  explicitly out of scope for this campaign.
