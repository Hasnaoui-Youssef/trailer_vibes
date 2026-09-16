# Finding 17 — Honest practical envelope (Phase 10)

Status: **a synthesis of this campaign's own already-measured numbers
(Finding 10's density study, Finding 07's fault case study), not new
board work.** Answers Phase 10's two questions plainly: how much
execution history the 2 KB on-chip buffer actually holds per workload
class, and which of Chapter 1's defect classes this campaign's own case
study demonstrates real help with versus which remain untested.

## How much history the buffer holds, per workload class

Finding 10's per-workload mean instructions/byte, converted to
instructions held by the buffer's 2048 usable bytes (excluding the
16-byte barrier, matching Finding 10's own convention). Min/max use the
same six repeat captures' own spread, not a separate measurement:

| Workload (class) | mean instr | min instr | max instr |
|---|---:|---:|---:|
| W2 call-chain (call-heavy) | 1710 | 1595 | 1763 |
| W5 DMA (idle/DMA-bound) | 2261 | 1729 | 3318 |
| W4 ISR (interrupt-driven) | 2922 | 2267 | 4010 |
| W1 baseline | 4579 | 3492 | 5782 |
| W3 branch-dense | 5222 | 3871 | 6582 |
| W6 overflow (densest, tightest branch loop) | 6193 | 4131 | 8714 |

(Updated after Finding 11's not-taken-branch fix — the buffer always
physically held this much history, this table just counts it correctly
now.)

Reading this plainly: the buffer holds roughly **1.6–1.8 thousand
instructions of call-heavy code**, up to **roughly 4–9 thousand
instructions of simple branch-dense code** — more than a 4x spread across
this campaign's own workload classes for the *same* physical 2 KB. A
statement like "the buffer holds N instructions" is meaningless without
naming which class of code N describes; this campaign's own workloads
are the evidence for that spread, not a general claim about the part.

**No wall-clock conversion is given.** Phase 10's original design asked
for this using Phase 9.5's cycle data; Phase 9.5 did not produce usable
cycle-per-instruction data (Finding 14 — the vendored OpenCSD build
cannot parse this hardware's timestamp/cyclecount packet stream). No
other finding in this campaign measured cycles per instruction on real
hardware, so a wall-clock figure here would be invented, not measured —
left out on that basis rather than approximated.

## Which of Chapter 1's defect classes this campaign actually tested

Chapter 1's Table \ref{tab:defect_classes} names five defect classes.
Checked against what this campaign actually ran, not what the pipeline
could in principle do:

| Defect class (Chapter 1) | Tested in this campaign? |
|---|---|
| Memory corrupted by a stray write | **Yes — Finding 07.** The only class this campaign directly demonstrates. |
| Fault taken with a damaged stack | No. W7's corruption hits a struct field via array overflow, not the CPU stack itself; no workload in this campaign damages the stack. |
| Race between a handler and the main program | No. W4 has a periodic, deterministic ISR, not an unsynchronized shared-state race with main-line code. The underlying capability this class needs — visible execution order across an exception boundary — is exercised incidentally (Finding 12's SysTick-boundary handling, Finding 15's exception naming), but no experiment here investigates an actual race defect. |
| Fault occurring before a debugger attaches | No. Every capture in this campaign arms trace and the debugger together, before the workload runs. Whether a fault that occurs while trace is armed but no debugger is attached would still be recoverable was never tried. |
| Defect appearing rarely, or only at full speed | No, and Finding 07 says so explicitly: W7's fault is deterministic by construction, chosen so N=1 is sufficient evidence — the opposite of a rare, hard-to-reproduce defect. |

One of five, tested directly. The other four are not contradicted by
anything found here, but this campaign supplies no evidence for them
either — stated plainly rather than left implied.

## The one class tested, bounded by this campaign's own numbers

Finding 07 chose a corruption-to-fault distance short enough to fit the
buffer "using Phase 3's density numbers to size the gap" — by design, a
best case. The instructions-per-buffer table above is what a *real*
stray-write defect would actually need to fit inside, and for anything
but a corruption immediately preceding its own fault, that is a real
constraint: a corrupting write buried behind, say, 2000 instructions of
call-heavy code (W2's own class) sits right at or past the edge of what
this buffer holds in its worst observed case (1595 instructions) — the
trace would show the fault, and a truncated prefix, but not necessarily
reach back to the actual corrupting store. The buffer's practical reach
for this defect class is therefore workload-density-dependent in exactly
the way Finding 10 measured, not a fixed number quotable independent of
the code being debugged.
