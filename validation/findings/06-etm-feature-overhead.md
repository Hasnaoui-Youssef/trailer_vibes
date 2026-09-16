# Finding 06 — ETM feature byte-overhead sweep

Status: **measured on real hardware, all six density workloads (W1-W6),
register-verified methodology.** Closes Phase 4, extended from its
original two-workload scope to all six after the extension was
specifically questioned and the original W2/W3 selection turned out not
to bound what it was meant to bound (see Method). This finding went
through three complete methodology revisions before landing on results
trusted enough to report; each is documented below rather than silently
overwritten, since the earlier numbers were genuinely wrong, not just
imprecise.

## Method

The plan's original design (compare raw capture byte counts across ETM
configurations at a fixed duration) cannot work on this hardware: every
configuration saturates the 2 KB ETF buffer well within any duration long
enough to be useful, so "how many bytes came out" stops being a function
of configuration at all (`capture_bytes` pinned at 2064 regardless of
config or duration).

The metric that works: hold the byte budget fixed at 2064 (one full
buffer) and compare **how many instructions decode from it** across
configurations. Fewer instructions for the same 2064 bytes means that
configuration spends more bytes per instruction — a real overhead cost,
measured through the actual decoder.

Getting a fair, controlled 2064-byte sample per configuration took three
iterations:

1. **First attempt**: CTI-triggered halt-on-full (Finding 04's mechanism)
   for one single fill per configuration, right after launch. Byte-for-byte
   identical output for several configuration pairs. Decoding the capture
   showed why: the entire captured window was boot code (`SystemInit`,
   `SCB_EnableDCache`) — the workload's own functions were never reached.
   The first fill after launch is dominated by cache/clock setup, not the
   workload.
2. **Second attempt**: added a breakpoint at the first statement of each
   workload's outer loop, discarding any CTI-triggered halt that occurs
   before that breakpoint is hit, then measuring the *next* clean fill
   after reaching it. Confirmed via decode that this reaches the
   workload's own functions — but the results were still byte-identical
   between some configurations. Traced this to a real bug in the script:
   the buffer content captured between the last re-arm and the breakpoint
   hit was never drained, so it silently prepended itself to the
   "measured" file on the next `trailerTraceDisable`.
3. **Third attempt**: explicitly drain-and-discard right after the
   breakpoint hit, before unlinking the capture path and starting the
   real measurement. This is the version whose results are reported below.

Even after all three fixes, `-retstack on` and `-branch-broadcast on`
still produced byte-identical output to baseline for W2/W3. Rather than
accept that at face value a fourth time, the actual hardware register was
read back directly (`TRCCONFIGR` at `0xE0041010`, via a target-scoped
`mdw`) across a real configure-then-resume cycle: `0x00000009` before
`-retstack on`, still `0x00000009` immediately after configuring (staged,
not yet committed — expected, matches the driver's own documented
design), then **`0x00001009` after the next resume** — bit 12 (`rs`) is
confirmed committed to real silicon. This rules out a driver bug
conclusively: the option is not being ignored, and defaults are as
designed (`branch_broadcast` defaults on in this driver, see
`external/openocd/src/target/arm_etmv4.c:1023`, so `-branch-broadcast on`
against a baseline that already has it on is expected to be a no-op —
`-retstack on` genuinely flips a bit from 0 to 1 and still produced
identical trace bytes for W2/W3 specifically).

**Extended from W2/W3 to all six workloads** after this two-workload
result was directly questioned: the original plan picked W2 and W3 as
"the two ends of the density spread," but that was a hypothesis, not
measured data, at plan-writing time — Finding 04's real density numbers
later showed the actual extremes are W2 and W6 (0.820 vs 2.459
instructions/byte), not W2 and W3. Steady-state markers for the four
added workloads, confirmed by reading each `main.c`: W1:19, W4:54, W5:77,
W6:21 (W2:48, W3:41 from the original pass).

## Second correction: baseline/retstack/branch_broadcast re-verified after Finding 11's not-taken-branch fix

The `baseline`, `retstack`, and `branch_broadcast` instruction counts
below were re-decoded after Finding 11's fix (not-taken conditional
branches are no longer dropped from the reconstructed instruction
listing). Every count moved upward — the same physical captures, now
counted completely — but every percentage in this finding's own analysis
(branch-broadcast's universal 0%, W4 retstack's small real cost) is
essentially unchanged, since taken and not-taken instructions belong to
the same static code regardless of ETM configuration. W4's retstack
effect moved from -2.9% to -3.1%, still small, still the only non-zero
result, still the same direction.

## Note: the `timestamp_cyclecount` column below was briefly, wrongly retracted

A later investigation (Finding 14) temporarily concluded this column
didn't reproduce, due to a bug in *that investigation's own* decode
script, not this finding's data. Finding 14 has since been rewritten: the
overflow counts in "New, unplanned finding" further down are confirmed
correct and reproduce exactly. The instruction counts in the table below
have also been updated in place to their current values, reflecting
Finding 11's later, unrelated not-taken-branch fix — the same update every
other column in this table already received.

## Results

Percentage change in instructions reconstructed from the same 2064-byte
buffer, relative to each workload's own baseline:

| Configuration | W1 baseline | W2 call-chain | W3 branch-dense | W4 ISR | W5 DMA | W6 overflow |
|---|---:|---:|---:|---:|---:|---:|
| baseline | 5246 (+0.0%) | 1555 (+0.0%) | 7034 (+0.0%) | 3933 (+0.0%) | 3451 (+0.0%) | 8134 (+0.0%) |
| `-retstack on` | 5246 (+0.0%) | 1555 (+0.0%) | 7034 (+0.0%) | **3811 (-3.1%)** | 3451 (+0.0%) | 8134 (+0.0%) |
| `-timestamp on -cyclecount on` | 2441 (-53.5%) | 841 (-45.9%) | 2774 (-60.6%) | 1313 (-66.6%) | 1729 (-49.9%) | 2441 (-70.0%) |
| `-branch-broadcast on` | 5246 (+0.0%) | 1555 (+0.0%) | 7034 (+0.0%) | 3933 (+0.0%) | 3451 (+0.0%) | 8134 (+0.0%) |

**Branch-broadcast is now a fully closed question, not just a two-workload
observation**: exactly 0.0% across all six workloads with no exception,
consistent with it defaulting on in this driver already.

**Timestamp+cyclecount is a real, large, and remarkably consistent cost**
across every workload class tested (branch-dense, call-heavy, idle/DMA-bound,
interrupt-driven) — -45.9% to -70.0%, the first real numbers behind Table
6.9's "timing discarded" limitation, and now with enough breadth to say
"consistently large" rather than "large in the two cases checked."

**Retstack shows a real, non-zero effect on exactly one workload: W4, the
only one with genuine interrupt activity, and the effect is a small cost
(-3.1%), not a saving.** Every other workload (W1, W2, W3, W5, W6 — none
of which have deep or ambiguous call/return patterns, and W4/W5's own
DMA/timer IRQs aside) shows exactly 0.0%. W4's ISR (`TIM6` update,
`HAL_TIM_PeriodElapsedCallback`) is the only source of genuine exception
entry/exit in this campaign's steady-state code — a fundamentally
different kind of "return" than a `BL`/`BX LR` pair. The direction is the
interesting part: retstack is meant to let a decoder skip predictable
return addresses, and here it *costs* bytes instead. A plausible reading:
exception return already gets cheap, address-implicit encoding without
retstack (the CPU always returns via a fixed `EXC_RETURN` mechanism, not
an address the encoder would otherwise need to spell out), and enabling
retstack adds bookkeeping overhead around exception boundaries without a
matching saving. This is a single N=1 measurement, not repeated — reported
as an observation worth a dedicated follow-up, not a settled mechanism.

**An earlier draft of this finding reported different W2/W3 numbers (+5.0%
and -17.3% for retstack) — those are now understood to be wrong**, from a
now-superseded duration-based (not CTI-triggered) capture methodology
whose start-of-capture point was uncontrolled. Not restated as a range or
average; the register-verified results above supersede them entirely.

## New, unplanned finding: the ETM's own trace generation overflows on call-dense code, and timestamp+cyclecount can induce it even where it wasn't present

**Both columns below stand as reported — see the note above: the
`timestamp+cyclecount overflow count` column was briefly and wrongly
disputed by a later investigation's own bug, since corrected.**

Every capture's gap breakdown was cross-checked for `overflow`-reason gaps
(`GapReason::kOverflow`, assigned directly from OpenCSD's own
`TRACE_ON_OVERFLOW`/`UNSYNC_OVERFLOW` classification of packets already in
the decoded stream — documented in OpenCSD's own headers as "overflow
packet(s) - need to re-sync," i.e. explicit recovery markers the *encoder*
embeds when it has already lost data, decoded faithfully, not inferred by
anything in this project):

| Workload | baseline overflow count | timestamp+cyclecount overflow count |
|---|---:|---:|
| W1 (no calls) | 0 | 0 |
| W2 (deep call chain) | 18 | 25 |
| W3 (single call/iteration) | 0 | 4 |
| W4 (ISR) | 0 | 23 |
| W5 (DMA + idle spin) | 0 | 11 |
| W6 (tightest branch loop, no calls) | 0 | 0 |

Two things are notable, and were checked against a wrong first theory
before being trusted (an earlier draft of this finding claimed 18
"buffer wraps" caused by CTI signal propagation delay — checked directly
by file size, exactly 2064 bytes, not 18×2064, and ruled out; see Finding
04's own corrected caveat for the retraction):

1. **At baseline, only W2 (the deep, non-recursive call chain) shows any
   overflow.** The likely mechanism: the ETMv4 unit's own internal
   packet-generation path — not the downstream TMC sink buffer — falls
   behind when the CPU produces trace-worthy events faster than the
   encoder can emit protocol bytes for them, and a call/return costs more
   protocol overhead to encode than a simple branch. W3's single call per
   outer iteration and W1/W6's call-free loops don't generate enough
   call density to trigger it.
2. **Enabling timestamp+cyclecount induces overflow in W3, W4, and W5 that
   wasn't present at baseline** (0→4, 0→23, 0→11), while W6 stays at zero
   even with timestamp+cyclecount on. Timestamp/cyclecount packets add
   per-instruction (or near-per-instruction) protocol overhead on top of
   whatever the code itself needs, and for workloads close to the ETM's
   own encoder throughput limit, that additional overhead alone is enough
   to push it into overflow — a real, previously-unmeasured *combined*
   cost of enabling detailed timing capture: not just fewer instructions
   per buffer, but a qualitatively different (lossier) capture for
   several workload classes. W6's immunity even under timestamp+cyclecount
   is consistent with it being the simplest possible loop (no calls, cheap
   branches) — likely the least demanding workload on the ETM's encoder of
   any tested here, despite being the *densest* by instructions/byte at
   baseline (Finding 04).

This does not invalidate the byte-overhead comparisons above (every
workload's four configurations are directly comparable to each other,
overflow count included as reported context) but it is a genuine hardware
behavior in its own right, and a sharper one than first thought: call-dense
code can outrun this ETMv4 implementation's own trace generation
independent of the TMC sink buffer, and turning on fine-grained timing
capture can push otherwise-fine workloads over that same edge. Worth a
dedicated, isolated follow-up (varying call density directly, e.g. a
workload with a tunable calls-per-instruction ratio) if this pipeline's
coverage or timing-accuracy claims ever need to account for encoder-side
data loss.

## Caveats, stated honestly

- **Retstack's non-zero result (W4) is a single N=1 measurement**, not
  repeated. The direction (a cost, not a saving) is the surprising part
  and is reported as an observation, not a fully explained mechanism.
- **N=1 per configuration throughout**, consistent with this campaign's
  position elsewhere on single measurements — six workloads times four
  configurations gives real breadth, but no repeat-based error bars.
- **The overflow-inducing effect of timestamp+cyclecount is observed, not
  isolated.** No experiment here varies call density and timing-capture
  independently to confirm they compound rather than coincide; the table
  above is the raw observation, not a controlled two-factor result.
