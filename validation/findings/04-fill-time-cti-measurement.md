# Finding 04 — CTI-triggered buffer fill-time measurement

Status: **measured on real hardware, 30/30 clean runs across all six density
workloads (W1-W6), 5 repeats each.** This is a new measurement not in the
original plan's Phase 5 design — it answers a different, complementary
question to Phase 5's original intrusiveness goal (see Caveats).

## What this measures

How many DWT `CYCCNT` cycles elapse, on-target, between a genuinely empty
2 KB ETF buffer and the buffer filling completely, per workload. Entirely
on-target and host-jitter-free: no wall-clock timing, no polling loop, no
`pause` request ever sent.

## Mechanism

Two OpenOCD `cti create` objects are created at launch time via
`openocd.rawCommands` (object *creation* works before OpenOCD's `init`
sequence; object *operations* like `write`/`enable` do not — confirmed by
testing, not assumed):

- `stm32h7x.cti_sys` — System CTI, AP0 (APB-AP), base `0xE00F1000`
- `stm32h7x.cti_m7` — Cortex-M7's own CTI, AP1 (AHB-AP), base `0xE0043000`

Wiring, done post-launch over a raw telnet connection to OpenOCD (object
*operations* do require `init` to have run first): System CTI's ETFFULL
input (`INEN2`) is gated onto channel 0, which is wired at the M7 CTI into
`OUTEN0` — the M7 core's own `EDBGRQ` (debug halt request) input. When the
ETF genuinely fills, the System CTI observes ETFFULL, propagates it over
the shared channel fabric to the M7 CTI, and the M7 CTI asserts EDBGRQ,
halting the core — a real, hardware-driven halt with no software polling
involved on either end.

Per repeat: `trailerTraceDisable` then `trailerTraceEnable` forces a
hardware drain and gives a genuinely empty buffer; any latched trigger
output from the previous repeat is acknowledged first (`INACK` write 1
then 0 — its reset value is 0, so a nonzero value means a trigger already
fired and must be cleared before it will fire again); `DWT_CYCCNT` is read
immediately before `continue` and again the moment a self-triggered
`stopped` DAP event arrives, with no `pause` ever sent.

Both CTIs are unconditionally disabled and cleared in a `finally` block on
exit — a stuck `EDBGRQ` left armed after one early test run blocked a
subsequent flash write until this was fixed, and is not something a
verification campaign can safely skip.

## Results

Per-workload deltas across 5 repeats (`validation/captures/<workload>/fill_time/results.json`):

| Workload | rep0 (cold) | rep1 | rep2 | rep3 | rep4 | steady-state mean (reps 1-4) |
|---|---:|---:|---:|---:|---:|---:|
| W1 baseline | 11228 | 5979 | 7859 | 7462 | 7458 | 7189.5 cyc (74.9 µs) |
| W2 call chain | 11324 | 6028 | 4513 | 3699 | 3670 | 4477.5 cyc (46.6 µs) |
| W3 branch-dense | 10777 | 6162 | 8030 | 7490 | 7518 | 7300.0 cyc (76.0 µs) |
| W4 TIM6 ISR | 11209 | 6095 | 4880 | 4006 | 3762 | 4685.8 cyc (48.8 µs) |
| W5 DMA | 12112 | 6756 | 5555 | 3724 | 3666 | 4925.2 cyc (51.3 µs) |
| W6 overflow | 10960 | 6609 | 12743 | 12834 | 12832 | 11254.5 cyc (117.2 µs) |

(cycles-to-µs at the board's confirmed 96 MHz core clock)

**rep0 is consistently ~1.5-2x every other repeat, in every workload.**
This is expected, not noise: `stopOnEntry` halts the target once at launch,
so rep0's run starts from the true program entry point and its trace
includes the one-time boot path (`SystemClock_Config`, cache enable,
`HAL_Init`) before ever reaching the workload's steady loop. Every later
repeat resumes from wherever the previous repeat's CTI halt landed — deep
inside the loop already — so reps 1-4 are the fair "steady loop" figure and
are reported separately from the cold-start one.

## Cross-check against instruction density (independent measurement, same workloads)

Phase 3's existing 200 ms "short" captures for each workload (collected in
an earlier session, via duration+pause, not this session's CTI mechanism)
were decoded offline with `trailer-trace-replay --metrics` against the same
board's `test_resources/etm_regs.json` baseline configuration. **Re-decoded
after Finding 11's not-taken-branch fix** (every count moved up; W1 also
reflects Finding 10's separate stale-firmware re-capture, so its number
here isn't directly comparable to an even earlier draft of this table):

| Workload | instructions / 2064-byte buffer | instr/byte | steady-state fill cycles |
|---|---:|---:|---:|
| W2 | 1741 | 0.844 | 4477.5 |
| W4 | 2285 | 1.107 | 4685.8 |
| W5 | 2174 | 1.053 | 4925.2 |
| W3 | 3900 | 1.890 | 7300.0 |
| W1 | 3519 | 1.705 | 7189.5 |
| W6 | 5639 | 2.732 | 11254.5 |

Sorted by density, the ranking matches the fill-time ranking exactly
(Pearson r = 0.989 across these 6 points, up from 0.958). This is the
expected relationship — a workload whose control flow needs more
instructions to produce the same number of trace bytes also takes more
cycles to fill the same buffer, assuming roughly comparable
cycles-per-instruction across workloads — and it comes from two genuinely
independent measurements (different session, different mechanism,
different capture) rather than one figure derived from the other.

## Caveats, stated honestly

- **This is not Phase 5's originally-planned intrusiveness measurement.**
  The plan's Phase 5 goal was: does enabling trace change the cycle count
  of a *fixed code region*, comparing trace-on vs trace-off. This experiment
  measures something else — how fast the buffer fills — and does not answer
  that question. See Finding 05 for the actual intrusiveness measurement,
  redone with a breakpoint-based design after the original live-poll design
  was confirmed broken (`readMemory` requires a stopped target regardless
  of memory strategy).
- **The density cross-check uses two different captures of the same
  workload**, not the same bytes twice. Phase 3's short captures are
  duration-based (run 200 ms, then pause and drain) rather than
  triggered-on-full; for the busier workloads (W2, W5) the buffer had
  already wrapped past full multiple times before the pause (visible in
  their own `overflow` gap counts), so `instructionsReconstructed` there is
  a steady-state ring-buffer snapshot, not a from-empty fill. That is the
  right basis for a *density* comparison — density is a property of the
  code's control flow, not of when the sample was taken — but it means the
  two tables above are not measuring the identical instant, only the same
  code pattern.
- **N=6, illustrative only.** Consistent with this campaign's stated
  position elsewhere: a strong correlation over six points is worth
  reporting honestly, not oversold as a powered statistical result.
- **The ~20 s per repeat observed here was a campaign-script bug, not an
  engine cost — found and fixed, see Finding 09.** `telnet_command()`
  (this campaign's raw-Tcl helper) waited a full 5 s socket timeout on
  every call regardless of how fast OpenOCD actually answered; the two
  `ack()` calls per repeat (4 telnet round trips) alone accounted for the
  entire ~20 s. Fixed by switching to an idle-timeout read. This never
  affected the correctness of any `CYCCNT` value above — those come from
  real reads at real halt events over the DAP protocol, a separate
  channel from the raw telnet calls — only this finding's own wall-clock
  framing was wrong. Finding 09 has the corrected, real engine-side
  drain/re-arm latency.
