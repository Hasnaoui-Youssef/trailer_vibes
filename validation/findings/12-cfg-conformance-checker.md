# Finding 12 — CFG-conformance checker (Phase 9.6)

Status: **built, run across every capture in the campaign via a new
persisted driver (`run_cfg_conformance_all.py` — the original run's exact
scope was never saved as a script, a gap fixed here) — 229,424
consecutive-instruction transitions checked across 61 captures, 229,407
clean (99.993%).** The plan asked for "explicit success criteria without
a test framework," run at real scale rather than on one fixture; this
closes that directly. The checker itself is `validation/analysis/cfg_conformance.py`,
a static, capture-independent tool operating on `trailer-trace-replay`'s
text output — no board needed to run it.

**Re-run after Finding 11's not-taken-branch fix, which broke a real
assumption in the checker itself** — see "A second checker bug" below.
The numbers in this finding are from that re-run, not the original one.

## Method

For every consecutive pair of instructions in a decoded trace, the
transition is legal if it is exactly one of:

1. **Fall-through** — the next address is the current instruction's own
   address plus its size. Since Finding 11's fix, a not-taken conditional
   branch is just an ordinary fall-through by this same rule — it now
   appears as its own row in the listing, so its transition to the next
   instruction needs no special case (see "A second checker bug" below;
   this replaces an earlier, now-removed rule that inferred a skipped
   not-taken branch by looking ahead in the static disassembly).
2. **Taken branch/call** — the next address matches the current
   instruction's statically-known branch target.
3. **Return** — the current instruction is a return, and the next address
   matches the top of an inferred call stack (pushed on `bl`/`blx`, popped
   on return).
4. **Indirect branch/call** — the target is runtime-computed (register
   operand), so any next address is accepted; this project's own
   `is_indirect` instruction metadata was found not to cover
   register-operand `blx` (e.g. `blx r3`, a DMA callback invocation in
   W5) — the checker derives indirectness itself from the mnemonic and a
   bare-register operand pattern rather than relying solely on that flag.

Gap and exception boundaries (see below) reset the inferred call stack
and are excluded from the check entirely — a discontinuity is not a CFG
transition by definition.

## A real, reusable engine addition made along the way

Getting a trustworthy result required knowing *where* gaps and exceptions
fall in the executed-instruction index, which `trailer-trace-replay` did
not expose in text mode before this work. Two additions:

- `PrintGapsText`, listing each `TraceGap`'s `instruction_index` and
  reason (already-existing data, just not printed in text mode before).
- **A new transform, `TraceTransform<model::ExceptionEvent>`**
  (`trace_model/exception_event.hpp`, `trace_transform/exception_event_transform.hpp`),
  mirroring `TraceTransform<TraceGap>`'s exact executed-instruction-count
  logic to correlate each `kException`/`kExceptionReturn` record to a
  position in the executed-instruction stream. This is genuinely reusable
  production code, not a one-off debug hack — it is the same underlying
  data Phase 9.5 (timing reconstruction) and Phase 9.7 (exception
  attribution) both need, built once rather than three times.

Without knowing exception boundaries, the checker's first full run showed
367 violations, concentrated in W2 (call-heavy, ~30 per capture). Every
one of those was a `SysTick` interrupt (`HAL_Init` enables it in every
workload, not just the interrupt-driven ones) firing mid-sequence,
confirmed by disassembly: a branch's own reported "next" address was the
handler's entry, and the handler's own return landed exactly back at the
interrupted branch's true target. Adding exception-boundary awareness
dropped this to 30; two further, smaller fixes (a regex that only matched
narrow `.n` conditional-branch suffixes, missing wide `.w` ones; and the
`blx`-with-register indirectness gap above) brought it to the final 13.

## A second checker bug: raw list position isn't the engine's executed-instruction index

Re-running this checker after Finding 11's fix (not-taken branches now
appear as their own rows) first produced 139 violations across 61
captures — a real regression from the previously-clean 13, traced to
source rather than accepted. The checker's `discontinuity_before_index`
set holds indices from `trailer-trace-replay`'s own "before
executed-instruction index N" text, which — like `TraceGap`/`ExceptionEvent`
throughout this campaign — counts only *taken* instructions. The
checker's own instruction list, however, is now raw text-row position,
which includes not-taken rows too. A gap or exception occurring after
several not-taken branches would be checked against the wrong row
entirely, since row position and executed-index had silently drifted
apart. Fixed by computing each row's own executed-index (a not-taken
conditional branch — identified the same way rule 1 above identifies it,
by its own transition being a fall-through rather than its own static
target — contributes zero to the running count) and matching
discontinuities against that, not raw position. This also let the
now-redundant "skipped not-taken branch" special case (and the static
`objdump` disassembly it needed) be removed outright — every not-taken
branch is now a plain, directly-checked fall-through, not an inference.

## Final result and the remaining 17, explained

| | count |
|---|---:|
| Captures checked | 61 |
| Transitions checked | 229,424 |
| Skipped at gap/exception boundaries | 134 |
| Clean | 229,407 (99.993%) |
| Violations | 17 |

Two explained categories, both already understood, neither new:

- **5 violations, all in `w4_isr/etm_sweep_cti/timestamp_cyclecount`** —
  this is the exact capture Finding 14 already documents as dominated by
  `UNSYNC_BAD_PACKET` decode corruption; a handful of CFG violations in
  its tiny surviving content is a direct symptom of that already-reported
  decoder limitation, not a new one.
- **12 violations, `pop`/`ldr`/`bl`-adjacent, in `w2_callchain` (3, at an
  overflow-recovery boundary) and `w7_fault` (9, inside HAL init/DMA
  routines)** — the same cause as the original 13: the checker's
  call-stack model is a simple LIFO primed only by `bl`/`blx`
  instructions *visible within the capture window*, which a capture that
  starts or ends mid-call-chain (common in these short, targeted
  captures, and at an ETM-encoder overflow-recovery seam for the W2 case)
  can leave missing an entry or holding a stale one. None of the 17 are
  unexplained jumps to arbitrary locations — every one is a legitimate
  return or call landing at a legitimate address, just not the one the
  checker's own simplified, window-bounded model expected. Not fixed
  further here (would need the checker to track call depth against
  window boundaries, real but not warranted for 17 out of 229,424).

## Phase 9.3 — gap and overflow correctness, and a correction to the plan's own assumption

The plan's Phase 9.3 assumed that holding a capture open past the 2 KB
buffer's physical capacity (W6's own design purpose) was the way to
observe a `kOverflow` gap in practice, replacing Table 6.4's untested "0
discontinuities." Checked directly, across all of W6's captures
(short/medium/long, both reps — every one of which, per this campaign's
own earlier finding, saturates and wraps the physical buffer many times
over): **zero `kOverflow` gaps, in any of them.** Only the standard
single `noSync` at the start of decoding appears.

This is not a failure to find something — it corrects an assumption. Per
Finding 06/11, `GapReason::kOverflow` reflects OpenCSD's own
`TRACE_ON_OVERFLOW`/`UNSYNC_OVERFLOW` packet classification, which traces
back to the *ETM encoder's own internal packet-generation FIFO* falling
behind — a call-density effect (confirmed present in W2, absent in
W1/W3/W4-baseline/W6). The TMC sink buffer physically wrapping (W6's
actual design purpose) is a different, invisible-to-the-decoder event:
by the time the buffer is extracted, the decoder only ever sees "a
contiguous 2 KB blob, uncertain where in the instruction stream it
starts" — handled by the ordinary start-of-decode `noSync`, not a
mid-stream `kOverflow` marker. So: Table 6.4's "0 discontinuities" claim
is now genuinely tested rather than assumed, and holds for this specific
overflow type (sink-buffer capacity) — while the *other* kind of overflow
(encoder-internal, call-density-driven) is real, observed, and reported
in Finding 06. These are two different physical phenomena that happen to
share one `GapReason` enum value, and conflating them would have been the
easy mistake to make here.

The CFG-conformance checker separately confirms every capture's gaps are
internally consistent: no CFG violation was ever found to straddle an
unmarked, unreported discontinuity — every actual jump the checker
couldn't otherwise explain was resolved once the correct gap or exception
boundary was accounted for (indexed correctly, per "A second checker bug"
above).

## Caveats, stated honestly

- **This checker is a Python analysis tool, not new engine test
  infrastructure** — it validates output text, and inherits any format
  drift risk that implies. It is not wired into CI.
- **The call-stack model is intentionally simple** (LIFO, primed only from
  visible `bl`/`blx`), and 12 of the 17 remaining violations are exactly
  its known blind spot playing out, not a new correctness question about
  the pipeline; the other 5 are a direct symptom of Finding 14's
  already-documented decoder limitation.
- **The `ExceptionEvent` transform**, built here, has since been reused
  directly by Finding 15's exception-attribution work — the "built once
  rather than three times" bet above paid off.
