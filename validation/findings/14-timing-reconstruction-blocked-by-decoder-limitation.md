# Finding 14 — Phase 9.5: trace-derived timing reconstruction, completed and cross-validated against DWT

Status: **done.** Phase 9.5's original goal (cycles-per-instruction-range,
W4's ISR entry-to-return cost) is achieved. This finding was originally
written concluding that the vendored OpenCSD build could not parse this
hardware's timestamp+cyclecount packet stream, and on that basis retracted
Finding 06's `timestamp_cyclecount` column. **Both conclusions were wrong.**
The decoder parses every cycle-count and timestamp packet in the stream
correctly. The actual blocker was that this investigation's own
re-verification script never passed the real ETM register snapshot to the
decoder, so every `timestamp_cyclecount` decode silently used the
decoder's disabled-by-default register fallback (`TRCCONFIGR` cci=0,
ts=0) instead of the real committed value (cci=1, ts=1). Finding 06's
original numbers were correct all along; this finding's own earlier draft
is what introduced the error and wrongly retracted them. Rewritten in
place rather than layering a further correction on top, since the earlier
draft's central conclusion no longer holds.

## Root cause, precisely

With the wrong `TRCCONFIGR` reaching OpenCSD's decode stage, genuine
ETM-encoder trace-buffer overflow recoveries get misclassified: the raw
discontinuity reason comes back as `UNSYNC_BAD_PACKET` (reason code 5)
instead of the correct `UNSYNC_OVERFLOW` (reason code 3), for what is, on
the wire, the same event — a real overflow, expected on a busy ISR-dense
or call-dense workload. Misclassified as a parse failure, the decoder also
fails to continue past the discontinuity, so the rest of the capture is
lost. Packet-level framing itself was checked directly against OpenCSD's
ETMv4 packet processor (`trc_pkt_proc_etmv4i.cpp`) and confirmed to be
entirely self-describing via per-byte continuation bits, with no
dependency on static decoder config, then verified byte-correct on a real
capture with a temporary raw-packet monitor
(`DecodeTree::addPacketPrinter`/`IPktRawDataMon`, added to `trace_decoder.cc`
and removed once confirmed) — ruling out any actual parse defect.

## Confirmation: Finding 06's original numbers were right

Finding 06's original `timestamp_cyclecount` overflow-gap counts (W2=25,
W3=4, W4=23, W5=11, W1=W6=0) match today's correctly-decoded numbers
**exactly**, with no adjustment needed at all — these are gap counts, not
instruction counts, so they're unaffected by Finding 11's later, unrelated
not-taken-branch fix. This is strong, independent confirmation that
Finding 06's original decode methodology was already correct; the
regression was introduced later, in this finding's own first
re-verification attempt (`verify_etm_sweep_results.py`, which never passed
`--etm-regs`), not in Finding 06 itself.

## Engine fixes shipped

1. **`TraceRecord` gained `cycle_count`/`timestamp` optional fields**,
   populated whenever a retained element's `has_cc`/`has_ts` sub-fields are
   set (OpenCSD's generic mechanism for a cycle count or timestamp riding
   on another element, e.g. an `InstructionRange`). Verified against the
   existing test suite with zero regressions.
2. **This alone was not sufficient**: ETMv4 (unlike ETMv3/PTM) only ever
   emits cycle counts and timestamps as their own dedicated
   `OCSD_GEN_TRC_ELEM_CYCLE_COUNT`/`TIMESTAMP` elements, never as a
   sub-field on another element (confirmed against both the ETMv4
   architecture specification, `IHI0064H_b` §5.2.14-5.2.16, and OpenCSD's
   `prog_guide_generic_pkts.md`) — so the fix above was correct but inert
   for this project's only decoder. `TraceRecordSink` now attaches both
   dedicated element kinds to the correct preceding instruction-range/
   exception/exception-return record, including
   `OCSD_GEN_TRC_ELEM_SYNC_MARKER`'s deferred-timestamp mechanism — not
   exercisable on this board (`TRCIDR0.TSMARK` requires ETMv4.6; this
   implementation is ETMv4.0) but implemented for ETMv4 generally, per
   this engine's own hardware-agnostic design goal, not just this board.
   `TraceRecord` gained a separate `timestamp_cycle_count` field: both the
   architecture spec and OpenCSD's docs are explicit that a cycle count
   embedded in a Timestamp element is not part of the cumulative
   cycle-count total and must not be summed with it. Covered by six cases
   in the new `trace_sink/test/trace_record_sink_test.cc`.
3. **`validation/analysis/verify_etm_sweep_results.py`** now passes
   `--etm-regs validation/analysis/etm_regs_timestamp_cyclecount.json` for
   the `timestamp_cyclecount` config. `baseline`/`retstack`/
   `branch_broadcast` are unaffected by this bug — confirmed empirically,
   identical output with or without the register snapshot — because their
   wire encoding never emits a Cycle Count or Timestamp packet at all.
4. **`src/trailer_trace_replay.cc`**'s JSON output gained a `records`
   array (index, kind, addresses, `cycleCount`/`timestamp`/
   `timestampCycleCount` when present) — the per-record data Phase 9.5
   itself needed, and generally useful for any downstream analysis that
   wants the decoded stream rather than just reconstructed instructions.

## Corrected timestamp_cyclecount numbers

Same code, same on-disk capture bytes as always used for this config,
only the register snapshot changes
(`validation/analysis/verify_timestamp_cyclecount_register_fix.py`):

| Workload | Instructions (wrong regs → correct regs) | noSync gaps | overflow gaps |
|---|---|---:|---:|
| w1_baseline | 2 → **2441** | 1 | 0 |
| w2_callchain | 25 → **841** | 1 | 25 |
| w3_branchdense | 11 → **2774** | 1 | 4 |
| w4_isr | 24 → **1313** | 1 | 23 |
| w5_dma | 11 → **1729** | 1 | 11 |
| w6_overflow | 6 → **2441** | 1 | 0 |

The overflow-gap counts (25, 4, 23, 11) match Finding 06's original,
never-wrong numbers exactly.

## Phase 9.5: W4 ISR entry-to-return cycle cost

**Methodology.** The existing ETM-sweep captures bound their window by
letting the CTI halt the core when the trace buffer fills, which does not
reliably capture one clean ISR span — a periodic timer firing many times
inside one 2064-byte window overflows repeatedly and fragments any single
invocation. `validation/campaign/run_isr_cycle_cost.py` instead brackets
the capture with two source breakpoints in `TIM6_IRQHandler`
(`stm32h7rsxx_it.c:206` entry, `:207` exit): arm and drain the trace buffer
exactly at entry, run to the exit breakpoint, disable tracing. This bounds
exactly one ISR invocation regardless of timer period or buffer capacity,
and reads the DWT cycle counter (`DWT_CYCCNT`) at both breakpoints as an
independent hardware ground truth for the same span — the same technique
Finding 05 used for DWT-based measurement.

**Results, three repeats:**

| Repeat | Trace-derived cycle sum | DWT hardware delta | Difference |
|---|---:|---:|---:|
| 1 | 316 | 324 | 8 (2.5%) |
| 2 | 316 | 324 | 8 (2.5%) |
| 3 | 314 | 322 | 8 (2.5%) |

The trace-derived sum is the total of every retained record's
`cycle_count` field across the capture (52 reconstructed instructions, one
`Exception` element marking the boundary, 21 instruction ranges). The
8-cycle gap is a fixed, systematic offset in every repeat, not noise —
consistent with a small, constant dispatch/threshold-quantization cost
(this hardware's cycle-count emission threshold, `TRCCCCTLR.THRESHOLD`, is
its architectural minimum of 4 cycles) rather than a measurement defect.

## Disposition

Phase 9.5 is complete: trace-derived cycle-cost reconstruction for a real
ISR entry-to-return span is accurate to within 2.5% of independent DWT
hardware ground truth, reproducible across three repeats. Finding 06's
`timestamp_cyclecount` results stand as originally published, confirmed
rather than retracted.
