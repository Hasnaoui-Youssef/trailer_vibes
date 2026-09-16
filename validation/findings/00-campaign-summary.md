# Validation Campaign — Consolidated Summary (Phase 12)

**Read this first.** This is the deliverable of the campaign — the thing
to review before anything touches `ST_PFE/`. Findings 01-18 hold the full
method, data, and honest caveats behind every result summarized here;
nothing below substitutes for them, it indexes and cross-references them.
No `.tex` file has been touched. Phase 13 (LaTeX integration) does not
start until this report is reviewed and separately approved.

**Post-hoc audit (after this report was first written):** the whole
`validation/` tree was re-checked for missing or stale artifacts before
handoff to a writing-focused session. One real gap was found and fixed —
Finding 11's raw shadow-trace data had never been persisted; it was
regenerated on real hardware and reproduced the original 99.75% result
exactly (see Finding 11's own update note). Cleanup also removed: the
superseded duration-based W2/W3 `etm_sweep/` captures and their
now-deleted script `run_etm_sweep.py` (Finding 06 already retracted their
numbers in prose; the superseded files no longer exist to be mistaken for
current data), an orphaned one-off firmware image (`cti_experiment`,
early CTI/IRQ bring-up, unreferenced by any finding), and `__pycache__`/
stray preview-PNG clutter. Nothing else was found missing — every other
finding's raw evidence, CSV, script, and figure checked out present and
internally consistent.

**A real defect fixed after this report was first written**: Finding 11's
own comparison had found that not-taken conditional branches were being
silently dropped from the reconstructed instruction listing — reported as
a limitation, not fixed, in the original pass. Fixing it changes
`instructionsReconstructed` campaign-wide (confirmed the shadow-trace
oracle from 99.75% to a clean 100.00%), so every finding that number
feeds — 04, 06, 10, 12, 13, 14, 17, and all 5 figures in 18 — was
re-verified against the fixed engine and updated in place. This also
caught and fixed a second, real bug: the CFG-conformance checker
(Finding 12) assumed raw list position matched the engine's own
executed-instruction index, an assumption the first fix broke. Every
finding below reflects the post-fix, re-verified state.

## Why this campaign exists

The reporter's remarks on the PFE draft converged on one point: the
validation chapter demonstrated the system works, but not how well, on a
thin base of evidence — one capture, one comparison using tools no longer
available. This campaign replaces that with real, on-hardware measurement:
six firmware workloads spanning call-heavy/branch-dense/idle-DMA/
interrupt-driven code, dozens of register-verified captures, a from-
scratch independent oracle, a static conformance checker run at real
scale, and several honestly-reported negative and blocked results
alongside the positive ones.

## Scope actually executed vs. the plan

| Phase | Status |
|---|---|
| 0 — shared infrastructure | done |
| 1 — OpenCSD config-compatibility | done (Finding 01) |
| 2 — firmware workload suite | done (Finding 02) |
| 3 — trace acquisition matrix | done, captures under `validation/captures/` |
| 4 — ETM feature/element sweep | done, all 6 workloads (Finding 06) |
| 5 — DWT intrusiveness | done (Finding 05) |
| 6 — TSan concurrency stress | done, with a real caveat — see below (Finding 16) |
| 7 — fault-handler case study | done (Finding 07) |
| **8 — dual-AP survivability** | **not started — explicitly skipped this session at your direction ("a side quest," out of scope), not attempted at all. The plan's protocol (Phase 8, isolated/higher-risk) is unchanged and still available if you want it pursued separately.** |
| 9.1-9.7 — offline analysis | done (Findings 10-15) |
| 10 — practical envelope | done (Finding 17) |
| 11 — visualization | done, 5 of 7 planned figures (Finding 18) |
| 12 — this report | now |
| 13 — LaTeX integration | not started, gated on your approval |

## Headline results

| # | Finding | Headline result | Status |
|---|---|---|---|
| 01 | OpenCSD config limits | No silent mis-decode on this hardware; COND already avoided by design | done |
| 02 | Firmware workload suite | 7 workloads (W1-W7) built and verified | done |
| 03 | Harness readiness | Campaign scripts unit-verified pre-board | done |
| 04 | CTI fill-time measurement | Clean, register-verified per-workload fill times, 30/30 runs | done; one earlier caveat retracted (see below) |
| 05 | DWT intrusiveness | Cycle delta unchanged with trace on vs. off, within counter resolution | done |
| 06 | ETM feature overhead | branch-broadcast 0% universally; retstack -3.1% on W4 only | done for baseline/retstack/branch-broadcast; **timestamp+cyclecount column retracted, see Finding 14** |
| 07 | Fault case study | Trace directly implicates the corrupting function; a real D-cache readMemory staleness bug found | done |
| 08 | Requirement coverage | FR1-FR7, NFR1 get new evidence; FR5 and FR7 now fully closed | done, updated after Finding 12 |
| 09 | Drain/rearm latency | ~85ms halt-detection latency, dominated by OpenOCD's own 100ms poll, not decode | done |
| 10 | Instruction density study | 0.84-3.02 instr/byte across workload classes; one stale-firmware bug found and fixed | done |
| 11 | Shadow-trace oracle | 100.00% concordance vs. an independent single-step oracle (99.75% before a real fix); FR5 closed | done |
| 12 | CFG-conformance checker | 229,424 transitions checked, 99.993% clean; new reusable `ExceptionEvent` transform; a real checker bug found and fixed during re-verification | done |
| 13 | Pipeline performance profile | Double-resolve fix measured: ~33% fewer `resolve()` calls, ~54-61% faster block transform | done |
| 14 | Timing reconstruction | W4 ISR cycle cost measured trace-side, within 2.5% of DWT hardware ground truth over 3 repeats; the earlier "decoder limitation" conclusion was a harness bug, now fixed | done |
| 15 | Exception attribution | New `trace_exception_attribution` module; validated against W4 and Finding 07's own HardFault | done |
| 16 | TSan concurrency stress | Reproducible LLDB-internal lock-order-inversion at init; no other race found under ~149s load | done, severity unconfirmed (stripped LLDB) |
| 17 | Practical envelope | 1/5 of Chapter 1's defect classes actually tested; buffer capacity is 4x workload-dependent | done |
| 18 | Visualization | 5 of 7 figures built; 2 skipped because their premise didn't hold | done |

## Corrections and retractions — read before quoting any single finding standalone

Several findings correct earlier ones in this same campaign. Treat the
later entry as authoritative in every case:

1. **Finding 04's "18 buffer wraps from CTI propagation delay" claim was
   retracted** after a direct physical-plausibility challenge — the real
   mechanism (ETM-encoder-internal FIFO overflow, unrelated to CTI timing)
   is documented in Finding 06.
2. **Finding 04's own "~20s per repeat" was a campaign-script bug**
   (`telnet_command()`'s fixed 5s timeout), not an engine cost — corrected
   in Finding 09, which has the real drain/re-arm latency (~85ms).
3. **Finding 06's `timestamp_cyclecount` column was briefly, wrongly
   retracted, then un-retracted.** An earlier draft of Finding 14 claimed
   the column didn't reproduce and blamed a genuine OpenCSD decoder
   limitation; both claims were wrong; the real cause was a register-
   passing bug in that investigation's own decode script. Finding 14 (now
   rewritten) confirms Finding 06's original overflow-gap counts were
   correct all along, and completes Phase 9.5 (W4's ISR cycle cost,
   cross-validated against DWT hardware ground truth) that the false
   blocker had stopped.
4. **Finding 10 fixed a real data-integrity bug** (W1's Phase 3 captures
   were decoded against since-rebuilt firmware) before its density numbers
   could be trusted — documented in place, not silently re-run.
5. **Finding 08's requirement-coverage table is now partially stale by
   its own design** (written before Findings 09-18 existed) — patched
   in place with a pointer: FR7 is now fully closed by Finding 12, not
   merely reinforced as Finding 08 originally stated.

## Real, unplanned findings — beyond what the plan asked for

Several results emerged from following the evidence rather than the
original design, and are strong material for the thesis in their own
right:

- **Not-taken conditional branches were being silently excluded from the
  reconstructed instruction listing** (ARM's own "N atom" convention) — a
  real, previously-undocumented pipeline defect, found and then fixed in
  the engine itself, not just written up (Finding 11).
- **A cached Cortex-M7's debug-port memory reads can be outright wrong**,
  not just incomplete, when the D-cache holds a write the CPU has already
  acted on — verified with an A/B test, not asserted (Finding 07).
- **The ETM's own trace-generation FIFO can overflow independently of the
  TMC sink buffer wrapping** — two physically distinct phenomena sharing
  one `GapReason::kOverflow` label; call-dense code (W2) triggers the
  encoder-internal kind at baseline, something no workload in the original
  plan was designed to surface (Finding 06/12).
- **A real production performance bug fixed and measured**: the
  `FunctionBlock` transform was silently re-walking already-reconstructed
  instructions from scratch; fixed with a zero-behavior-change overload,
  ~33% fewer `resolve()` calls and ~54-61% faster block transforms
  measured before/after on the same captures (Finding 13).
- **A suspected third-party decoder limitation turned out to be a harness
  bug, caught by re-checking the premise rather than accepting it**:
  Finding 14's original conclusion, "OpenCSD cannot parse this hardware's
  timestamp+cyclecount packets," was wrong — the real cause was a missing
  `--etm-regs` argument in that investigation's own decode script. Finding
  14 has been rewritten: Phase 9.5 is complete, and W4's trace-derived ISR
  cycle cost matches independent DWT hardware ground truth within 2.5%.
- **A reproducible LLDB-internal lock-order-inversion**, caught 3/3
  attempts under `linux-tsan`, traced to a specific initialization
  function via source inspection (Finding 16) — plausibly benign but not
  confirmable further without a symbol-ful LLDB build.
- **Two new, reusable engine additions** made along the way rather than
  as one-off analysis hacks: `TraceTransform<model::ExceptionEvent>`
  (Finding 12) and `trace_exception_attribution` (Finding 15), both
  already covered by the existing test suite.

## What this campaign does not establish — stated plainly

- **No determinism claim anywhere** — repeats are statistical replicates
  for error bars only, per this campaign's standing position from the
  plan's own self-review.
- **Phase 8 (dual-AP survivability) was never attempted** — the
  architecturally-grounded hypothesis from the plan is neither confirmed
  nor refuted.
- **Only 1 of Chapter 1's 5 defect classes was actually tested** (memory
  corrupted by a stray write, Finding 07) — the other four (damaged
  stack, handler/main race, fault-before-attach, rare/full-speed-only
  defects) have no evidence from this campaign either way (Finding 17).
- **Wall-clock cycle-cost timing exists for one case, not the whole
  campaign** — Finding 14 measures W4's ISR entry-to-return cost
  trace-side, cross-validated against DWT (3 repeats, within 2.5%).
  Finding 17's practical-envelope discussion predates this and remains
  instructions-only; extending cycle-cost measurement to the rest of the
  campaign's workloads is unstarted follow-up work.
- **2 of 7 planned figures were not built** (W6 overflow timeline, W4 ISR
  cycle-cost) because the phenomena they were meant to show don't occur
  or aren't measurable with this campaign's data (Finding 18).
- **The TSan stress run is bounded by memory, not by an exhaustive
  duration** — a real constraint on this development machine (no swap),
  documented rather than hidden (Finding 16).
- **Several results are N=1** (Finding 06's retstack effect, Finding 07's
  fault case, Finding 16's lock-order-inversion severity) — flagged
  individually in each finding, not smoothed into false confidence here.

## Requirement coverage

See Finding 08 for the full FR/NFR table. Net effect of this campaign:
**FR1-FR7 and NFR1 get real new evidence; FR5 and FR7 are now fully
closed** (previously the report's own flagged weak points — FR5's
unavailable vendor-tool comparison, FR7's untested "0 discontinuities"
claim). FR9/FR10 get incidental reinforcement, including a real new
caveat on FR10 (the D-cache staleness finding above). FR8, FR11-13,
NFR2-3 are untouched by this campaign, exactly as before.

## Engine code changed in service of this campaign

All changes below are covered by the existing test suite with **zero
regressions** — the frozen 114-instruction/6-function-block/2-gap
`trace_provider_test` oracle is bit-for-bit unchanged, and the VS Code
extension's real-hardware test suite passes 61/61 (see session record;
re-verify with `cmake --build build` + the existing test binaries + `npm
test` in `trailer/` before trusting this claim again if more time has
passed).

- `trace_model/exception_event.hpp`, `trace_transform/exception_event_transform.hpp` — new, reusable (Findings 12, 15)
- `trace_transform/function_block_transform.hpp`, `trace_provider/trace_session.cc` — double-resolve fix (Finding 13)
- `trace_provider/trace_session.cc`, `src/trailer_trace_replay.cc` — stopped silently dropping not-taken conditional branches from the returned/printed instruction listing (Finding 11); frozen oracle moved 114 → 118 instructions accordingly
- `trace_sink/trace_record.hpp`, `trace_sink/trace_record_sink.{hpp,cc}`, `trace_sink/test/trace_record_sink_test.cc` — cycle_count/timestamp field retention, dedicated Cycle Count/Timestamp/Sync Marker element attachment, and the non-cumulative `timestamp_cycle_count` field (Finding 14)
- `trace_decoder/trace_decoder.{hpp,cc}` — dropped-element-kind tracking (Finding 01)
- `trace_exception_attribution/` — new module (Finding 15)
- `src/trailer_trace_replay.cc` — restored and substantially extended offline CLI (`--metrics`, `--dump-elements`, `--svd`, gap/exception text output, and a per-record `records` array in the JSON output for Phase 9.5, Finding 14)
- `CMakeLists.txt`, `CMakePresets.json`, `modules/providers/trace/CMakeLists.txt` — `trailer-trace-replay` target, `linux-tsan` preset, new module wiring

## Artifact index

- `validation/campaign/*.py` — 15 board-driving scripts (capture, ETM sweep, intrusiveness, fault case study, TSan stress, CTI experiments, `run_isr_cycle_cost.py`)
- `validation/analysis/*.py` + `*.csv` — 10 offline analysis scripts (density, ETM overhead, pipeline metrics, CFG conformance and its driver, shadow-trace comparison, figure generation, `verify_etm_sweep_results.py`, plus `verify_timestamp_cyclecount_register_fix.py`)
- `validation/captures/` — raw capture bytes, engine logs, and metadata for every board session, organized by workload
- `validation/figures/*.pdf` — 5 vector figures (Finding 18)
- `validation/findings/01-18` — this report's supporting evidence, one file per topic

## Recommendation

This report, Findings 01-18, and the 5 figures are ready for review.
Phase 13 (edits to `chap_06.tex`, `annexes.tex`'s test inventory, and
Table 6.9's limitations table) is not started and will not begin without
your explicit approval after reviewing this material.
