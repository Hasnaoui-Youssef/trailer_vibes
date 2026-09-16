# Finding 07 — Fault-handler root-cause case study

Status: **measured on real hardware, W7, clean capture.** Closes Phase 7.
Along the way, three real bugs in the campaign scripts were found and
fixed (not smoothed over — see Bugs found), and a genuinely new,
independently-verified finding about debug memory reads on this
cached core emerged that was not in the original plan.

## Setup

W7's `Process()` writes one byte past `record.buffer[8]` on every call,
landing on the low byte of the adjacent `record.on_complete` function
pointer, clearing its bit 0. `record.on_complete()` — an indirect call
through a now-invalid (non-Thumb) address — is an architecturally
guaranteed INVSTATE fault on Cortex-M, deterministic on every pass.

## Bugs found and fixed in the campaign script, stated plainly

1. **The ETF sink's `configure -output` command was issued via
   `openocd.rawCommands`, which executes before OpenOCD's `init`
   sequence** — the same timing bug already fixed in `run_capture.py`
   earlier in this campaign, missed here because this script predates
   that fix. Moved to a post-launch telnet call.
2. **The original design slept 1 s after `continue` before pausing,
   expecting the fault to happen "well under 1s" and then just spin.**
   It does — `HardFault_Handler` is `while(1);` — but that means the CPU
   spends the *entire* 1 s continuously re-tracing the same few
   instructions, wrapping the 2 KB buffer dozens of times over
   (confirmed: 43 `overflow` gaps in the first capture) and completely
   erasing the pre-fault sequence this case study exists to demonstrate.
   `trailer-trace-replay` on that first capture showed exactly one
   degenerate function block — the spin loop, nothing else. Fixed by
   setting a breakpoint at `HardFault_Handler`'s entry instead of
   sleeping, so the halt happens as soon as the vector is taken, before
   the spin has a chance to overwrite anything.
3. **The TMC sink opens its output file in append mode
   (`fopen(..., "ab")` in `arm_tmc.c`), and this script reused one fixed
   output path across repeated runs during debugging.** A second run
   appended its 2064 bytes onto the first run's leftover file, producing
   a corrupted 4128-byte blob that decoded as garbage (spurious `NoSync`
   gaps at the internal seam). Fixed by unlinking the capture path before
   each run.

## Comparison

**State-at-halt alone**, at the breakpoint on `HardFault_Handler`'s
entry:

```
CFSR = 0x00020000  (UFSR bit 1 = INVSTATE — the fault type is legible)
HFSR = 0x40000000  (FORCED — an unhandled configurable fault escalated to HardFault)
ICSR & 0x1FF = 3   (VECTACTIVE = HardFault)
record.on_complete = 0x00000000
```

The fault type (INVSTATE, escalated because `UsageFault` itself isn't
separately enabled here) is legible from the registers alone. **The
corrupted pointer's value is not** — reading `0x00000000` is not even the
correct corrupted value (see below), and nothing in this view says which
instruction produced it, or that `Process()` is involved at all.

**With instruction trace** (`trailer-trace-replay` on the same halt's
capture, clean this time — 2 gaps total, both the expected startup
`noSync`/`traceOn`, no overflow): the reconstructed sequence shows
`Process` executing its copy loop (`main.c:38`/`40`, repeated), returning
(`main.c:42`), and the very next attributed instructions are
`main.c:58` — `record.on_complete();`, the exact faulting call — where
the trace ends. This directly implicates `Process()` as the step between
a known-good assignment and the fault, from the instruction sequence
alone. Note what this is and isn't: ETMv4 data tracing is not enabled on
this pipeline (Finding 01 — OpenCSD hard-rejects it), so the trace shows
*which code ran, in what order*, not the corrupted *value* itself. That
is still real, useful root-cause evidence — it answers "what ran between
the good state and the fault" — but it is evidence about control flow, not
a data-value trace.

## New finding: a live memory read can be outright wrong here, not just incomplete

`record.on_complete` read as `0x00000000` at halt — not the expected
corrupted value. Verified, not assumed, with an A/B test: W7's
`SCB_EnableDCache()` call was temporarily removed, rebuilt, reflashed, and
the identical case study rerun. With the D-cache disabled, the same
memory read returned `0x08000300` — exactly the predicted corrupted value
(`SafeAction`'s address `0x0800033D` with its low byte cleared). Reverted
immediately after confirming; the reported case study above ran with the
D-cache enabled, matching the workload's real, intended configuration.

Two independent read paths agreed on the wrong value before the fix (the
engine's own `readMemory` and a raw standalone `openocd mdw`), ruling out
an engine-specific bug: this is a real property of debugging a Cortex-M7
with its write-back D-cache enabled. `record.on_complete = SafeAction`
and `Process()`'s corrupting write both go through the CPU's own D-cache;
the CPU's own subsequent load (and hence its own fault) sees the correct,
up-to-date value, but a debug-port memory read bypasses the cache and
reads whatever is still physically in SRAM — here, the pre-write zero
from `.bss` init, since nothing forced that cache line to be written back
before the halt. This sharpens Chapter 1's own motivation beyond what was
originally planned: state-at-halt is not just silent about *which
instruction* corrupted a value, it can be *factually wrong* about the
value's current contents, on exactly the kind of core (cached Cortex-M7)
this project targets — and it is a real property of the hardware and
debug port, not something specific to this engine.

## Caveats, stated honestly

- **N=1, but the fault is deterministic by construction** (the off-by-one
  always corrupts the same byte on the first pass), so a single instance
  is the right amount of evidence here, not an undersized sample.
- **The corruption is inferred from control flow, not observed as a data
  value.** The trace does not show the literal byte written; it shows
  that `Process()` ran, in the right place, between the assignment and
  the fault. This is real and useful, but a reader should not expect a
  hex dump of the write itself out of an ETMv4 instruction trace on this
  configuration.
- **The D-cache finding was reproduced with one A/B pair (cache on,
  cache off), not repeated.** The mechanism (write-back cache, debug port
  reads bypass it) is standard, well-understood Cortex-M behavior, not a
  novel physical claim, so a single confirmatory pair is reasonable
  evidence for what is already an expected effect — but it was verified
  here, not asserted from documentation alone.
