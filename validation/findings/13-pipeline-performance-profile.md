# Finding 13 — Pipeline performance profile (Phase 9.4)

Status: **measured, real before/after data for the double-resolve fix,
honest scoping of what this campaign's data can and can't show about
scaling.** Uses `trailer-trace-replay`'s existing `--metrics` sidecar and
`resolveCalls` counter (both already built in Phase 0.2, not new here). A
new script, `validation/analysis/aggregate_pipeline_metrics.py`, runs it
across every baseline-config density-matrix capture and writes the raw
per-capture rows to `validation/analysis/pipeline_metrics.csv`.

## What this campaign's data cannot show, stated up front

The plan asked for "decode time vs. byte count." Every capture in this
campaign is a fixed 2064-byte TMC buffer — `bytesIn` has **zero variance**
across all 36 captures (mean 2064.0, stdev 0.0). Byte count was never a
free variable here, so a decode-time-vs-bytes relationship literally
cannot be computed from this data; claiming one would be fabricating a
trend from a constant.

What *does* vary, substantially, within that fixed byte budget is how
much executed history each workload packs into it: `traceRecords` ranges
442–1764, `instructionsReconstructed` ranges 1608–8783 across the same 36
captures. That variation is real and usable — the question this section
actually answers is decode/transform time vs. *instruction/record count*,
within a fixed-byte-budget regime, not vs. byte count.

**Numbers below reflect a re-run after Finding 11's not-taken-branch fix**
(`instructionsReconstructed` now counts every reconstructed instruction,
not just taken ones — see Finding 11). `traceRecords` and `resolveCalls`
are unaffected by that fix (both were already computed over the full,
unfiltered walk beforehand); `instructionsReconstructed` and every
correlation involving it shifted, and are updated throughout this finding.

## Method

`aggregate_pipeline_metrics.py` decodes every `short|medium|long` x
`rep0|rep1` capture (36 total, all 6 workloads, baseline ETM config only —
the `etm_sweep` captures use different configs and would confound a
same-config timing comparison) via `trailer-trace-replay --metrics`,
parses each JSON sidecar, and aggregates.

## Aggregate timing/count profile (n=36)

| metric | mean | stdev | min | max |
|---|---:|---:|---:|---:|
| bytesIn | 2064.0 | 0.0 | 2064 | 2064 |
| decodeMs | 1.394 | 0.441 | 0.859 | 2.500 |
| precomputeMs | 12.716 | 2.181 | 10.681 | 18.062 |
| transformInstructionsMs | 4.427 | 2.408 | 1.831 | 11.021 |
| transformBlocksMs | 3.146 | 1.510 | 1.473 | 7.031 |
| transformGapsMs | 0.012 | 0.006 | 0.005 | 0.033 |
| traceRecords | 966 | 382 | 442 | 1764 |
| instructionsReconstructed | 3844 | 1941 | 1608 | 8783 |
| resolveCalls | 7286 | 3611 | 3195 | 16689 |

`precomputeMs` (ELF/DWARF ingestion, once per capture) dominates every
individual stage — expected, since it does a fixed amount of symbol/DWARF
work per decode call regardless of trace size, while decode/transform
scale with the trace's own content.

## Decode/transform time vs. instruction count (illustrative, small-N per-workload buckets)

Pearson r across the same 36 points:

| relationship | r |
|---|---:|
| instructionsReconstructed vs. resolveCalls | 0.999 |
| instructionsReconstructed vs. transformBlocksMs | 0.989 |
| instructionsReconstructed vs. transformInstructionsMs | 0.988 |
| instructionsReconstructed vs. decodeMs | 0.972 |

The near-1.0 `resolveCalls` correlation is a structural sanity check, not
a discovery: `resolve()` is called roughly once per reconstructed
instruction by design, so this number confirms the instrumentation
behaves as built rather than revealing anything new. The other three are
now uniformly strong (0.97–0.99, up from 0.57–0.96 before Finding 11's
fix) — `instructionsReconstructed` counting every reconstructed
instruction rather than only the taken ones removes a source of noise
that previously weakened these relationships, particularly `decodeMs`'s
(0.569 → 0.972): OpenCSD's own packet-level decode cost tracks the *full*
instruction stream it walks, not just the subset that used to survive
downstream filtering. Six discrete workload classes, not a controlled
sweep — illustrative, per this campaign's standing framing, not an
inferential regression.

## The double-resolve fix — measured before/after, not assumed

`function_block_transform.hpp`'s original `TraceTransform<FunctionBlock>`
took `TraceRecord`s and internally re-ran
`Transform<ReconstructedInstruction>` from scratch — a second full walk,
calling `resolve()` again for every instruction, on top of the walk a
caller holding `DecodedIncrement`/`executed` had usually already paid for
once. The fix (already shipped this session in `trace_session.cc`'s
production path and `trailer_trace_replay.cc`) split it into two
overloads: one taking already-reconstructed instructions directly (no
re-walk), and a convenience overload for a caller with only records in
hand (which still does the walk, but exactly once, not twice).

To get a real measured delta rather than an algebraic guess, the CLI's
`FunctionBlock` call site was temporarily reverted to the old
double-walk pattern (`xform::Transform<model::FunctionBlock>(records,
resolve)`), rebuilt, measured, then restored to the fixed overload and
rebuilt again — both binaries run against the same three capture files
(`long/rep0` for W1, W2, W6 — chosen to span the density range: W1
baseline, W2 lowest density/call-heavy, W6 highest density/branch-dense).

| workload | resolveCalls (before → after) | reduction | transformBlocksMs (before → after) | reduction |
|---|---:|---:|---:|---:|
| W1 baseline | 15003 → 9729 | 35.2% | 10.01 → 3.97 | 60.4% |
| W2 call-chain | 5161 → 3433 | 33.5% | 3.81 → 1.77 | 53.5% |
| W6 overflow | 25472 → 16689 | 34.5% | 18.27 → 7.14 | 60.9% |

`instructionsReconstructed`, `functionBlocks`, and `gaps` counts are
bit-for-bit identical before and after for all three captures — this is a
pure performance fix, verified to change zero output, consistent with the
existing test suite's frozen (now 118/6/2, after Finding 11's separate
fix) `trace_provider_test` oracle also being unchanged by *this* fix.
`resolveCalls` before/after is identical to the figures measured prior to
Finding 11's fix — expected, since `resolve()` was already walking every
reconstructed instruction (taken or not) before that fix, only the
downstream filtering changed. The ~1/3 reduction in `resolve()` calls is
consistent across all three workloads despite a 5x spread in absolute
trace size, as expected from the fix eliminating one specific redundant
walk regardless of scale; `transformBlocksMs` is wall-clock timing on a
shared dev host and varies somewhat run to run (the reduction was 57–80%
when first measured, 54–61% here), but the direction and rough magnitude
are consistent every time this has been measured.

## Caveats, stated honestly

- **Wall-clock timing on this development host**, not a claim about
  embedded-target timing — this section profiles the offline pipeline
  itself (decode/transform stages), not anything about the MCU. Real
  target-timing reconstruction is Phase 9.5's job, using ETM-captured
  cycle/timestamp elements, not host wall-clock measurement of any kind
  ([[feedback_no_host_side_mcu_timing]]).
- **n=36 spans six discrete workload categories**, not a continuous,
  controlled independent variable — correlations above are illustrative,
  matching this campaign's standing framing for small-N results.
- **The byte-count axis is degenerate in this campaign's data by
  construction** (every capture saturates the fixed 2KB sink), not a
  limitation of the instrumentation — `--metrics` would report real
  variation in `bytesIn` given differently-sized captures; none exist
  here to measure against.
