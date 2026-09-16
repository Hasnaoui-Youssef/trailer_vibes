# Finding 05 — Intrusiveness: DWT cycle-counter measurement

Status: **measured on real hardware, W1, 49 deltas per condition.** This
closes the original Phase 5 objective — the CTI-based fill-time work in
Finding 04 answers a different question and does not substitute for this.

## Method

A single breakpoint at `main.c:19` (`int x = 0;` — the first statement of
every `while(1)` iteration in W1's now-cleaned-up baseline, see below) is
hit repeatedly via `setBreakpoints` + `continue` in a loop. `DWT_CYCCNT` is
read via `readMemory` at every stop — valid, since the target is genuinely
halted at a real breakpoint, unlike the original design's live poll. The
per-iteration cost is the delta between consecutive hits, so the one-time
boot path before the first hit (cache enable, `HAL_Init`,
`SystemClock_Config`) never enters any reported figure — every delta is
already loop-steady-state by construction. 49 deltas (50 breakpoint hits)
were collected with trace disabled, then 49 more with trace continuously
enabled across every hit, on the same launched session.

This differs from Finding 04's CTI mechanism in what it measures, not just
in method: Finding 04 asks how fast the buffer fills; this asks whether
tracing changes the target's own execution timing. A debug halt+resume is
part of the measurement in both conditions here, symmetrically — the
comparison is trace-on-with-halts vs. trace-off-with-halts, not
halts vs. no halts.

## Result

| Condition | n | min | median | max | mean |
|---|---:|---:|---:|---:|---:|
| Trace disabled | 49 | 106 | 106 | 108 | 106.04 |
| Trace enabled | 49 | 106 | 106 | 106 | 106.00 |

**Delta (median-to-median): 0 cycles.** The two distributions are
indistinguishable at single-cycle resolution — enabling ETMv4 instruction
trace produces no measurable change in this loop's execution cost. This is
the first cycle-accurate, on-target evidence for Chapter 2's
non-intrusiveness claim; previously it was argued architecturally
(dedicated trace hardware, no CPU-side instrumentation) but never measured.

## Caveats, stated honestly

- **Single workload, single fixed region, ~106 cycles.** This is W1's
  short nested-branch loop body only. A 2-cycle spread (108 vs 106) is
  already close to the counter's own resolution for a region this short;
  a genuinely intrusive effect smaller than a couple of cycles per
  iteration would not be visible here. Extending this to a longer or
  more varied region (e.g. W2's call chain) would sharpen the claim
  further but was not done in this pass.
- **The comparison method itself halts the target on every sample, in
  both conditions.** This measures whether *trace itself* is intrusive
  relative to an already-instrumented (breakpoint-sampled) baseline — a
  fair, like-for-like comparison for that question — not whether
  debugging in general is free of overhead (it manifestly is not: no
  wall-clock claim is made about how long the whole sampling loop took).
- **W1's firmware changed since Finding 02 was written.** Finding 02's
  earlier build-size table for W1 was checked against a version of
  `main.c` that had since grown a since-removed DWT self-timing block
  (added for an earlier, abandoned live-poll design). That instrumentation
  is gone; W1 is back to `text=4832 data=12 bss=1568`, byte-for-byte the
  same as the existing dummy project, matching what Finding 02 originally
  claimed and needed no longer requires correction.
