# Finding 10 — Instructions-per-byte density study (Phase 9.1)

Status: **measured on real hardware, all six workloads, offline analysis
of Phase 3's existing captures.** No board work in this finding itself,
but one real data bug was found and fixed in the existing Phase 3
artifacts before the numbers could be trusted — documented below.

**Re-run after Finding 11's not-taken-branch fix**: every number below is
from a fresh decode against the current engine, which now counts every
reconstructed instruction rather than only taken ones. Densities moved
upward (not-taken branches were always physically present in the trace,
just previously uncounted) and the ranking is unchanged.

## A real bug found first: W1's Phase 3 captures were decoded against the wrong firmware

Decoding `w1_baseline/short/rep0/capture.bin` against the current
`w1_baseline.elf` gave 733 instructions for 2064 bytes (density 0.355) —
wildly inconsistent with Finding 04's independently-measured, register-
verified figure for the same workload (1.978 instr/byte, from a clean
CTI-triggered fill). Every other workload's fresh decode matched Finding
04's own numbers for the identical files exactly (W2: 1692, W3: 3438, W4:
1939, W5: 2170, W6: 5075 instructions) — only W1 was wrong.

Cause: W1's firmware was rebuilt earlier this session (the DWT
self-timing instrumentation added for an abandoned design, then found
and removed — see Finding 05) *after* these Phase 3 captures were taken.
The capture bytes are a snapshot of whatever code was on the board at
capture time; decoding them against a since-changed ELF silently produces
wrong instruction counts and wrong source attribution, with no error —
the decoder has no way to know the ELF it's given doesn't match the trace
it's decoding. No other workload's firmware changed after its Phase 3
captures were taken, so no other workload was affected.

Fixed by re-capturing W1's Phase 3 data (short/medium/long, 2 reps each)
against the current, correct firmware, using `run_capture.py`. This also
surfaced a second, smaller bug in that script: `run_capture.py` never
unlinked its capture path before flashing, and the TMC sink opens its
output file in append mode — a stale file from the same run directory
would have silently prepended itself to the new capture. Fixed the same
way as the equivalent bug found and fixed earlier in `run_fault_case_study.py`
and `run_etm_sweep_cti.py`. Re-decoded W1 afterward: mean density 1.889
instr/byte, matching Finding 04's 1.978 within the spread these
duration-based captures already show for every other workload (below) —
consistent, not coincidentally close.

## Method

Per workload, six existing Phase 3 captures (short/medium/long duration,
2 repeats each — all pinned at 2064 bytes, since every workload saturates
the ETF well within even the shortest duration) decoded with
`trailer-trace-replay --metrics`. Density = instructions reconstructed /
bytes in, per capture; mean and standard deviation across the six give the
reported figure and its error bar. Per this campaign's standing position:
these repeats are a statistical replicate for the error bar, not a
determinism claim — each duration/repeat samples the workload's own
steady-state loop starting from whatever point the fixed-duration host
sleep happened to land at, not a controlled starting point.

## Results

| Workload | mean instr/byte | stdev | min | max | n |
|---|---:|---:|---:|---:|---:|
| W2 call-chain | 0.835 | 0.029 | 0.779 | 0.861 | 6 |
| W5 DMA | 1.104 | 0.281 | 0.844 | 1.620 | 6 |
| W4 ISR | 1.427 | 0.294 | 1.107 | 1.958 | 6 |
| W1 baseline | 2.236 | 0.481 | 1.705 | 2.823 | 6 |
| W3 branch-dense | 2.550 | 0.585 | 1.890 | 3.214 | 6 |
| W6 overflow | 3.024 | 0.954 | 2.017 | 4.255 | 6 |

Ranking (densest-encoded to most-instructions-per-byte): W2 < W5 < W4 <
W1 < W3 < W6 — unchanged by the not-taken-branch fix, though the gap
between W4 and W5 widened (they were a near-tie before). This mostly
matches Finding 04's clean-fill ranking (W2 < W4 < W5 < W3 < W1 < W6),
with W4 and W5 swapped — expected, since these are two different
measurement bases (Finding 04: one controlled clean fill per workload;
this finding: six uncontrolled-start duration-based samples averaged)
that should agree in shape, not swap-for-swap in a near-tie between two
similar workloads. W5's own numbers are identical before and after the
fix (1.104 mean, same min/max) — its captured window happens to contain
no not-taken conditional branches, unlike every other workload here.

## Branch density vs. instructions/byte (illustrative, per the plan's own framing)

Static branch density per workload — branch/call/return-classified
instructions (via `arm-none-eabi-objdump`, not the C++ `ProgramDisassembler`
directly — see caveat) as a fraction of total instructions, restricted to
each workload's own functions (excluding shared HAL/boot code common to
all of them):

| Workload | branch-classified / total | density |
|---|---:|---:|
| W6 | 7/68 | 0.103 |
| W1 | 7/67 | 0.104 |
| W3 | 12/88 | 0.136 |
| W2 | 17/98 | 0.173 |
| W4 | 25/105 | 0.238 |
| W5 | 45/164 | 0.274 |

Pearson r (static branch density vs. mean instructions/byte) = **-0.767**
across these six points — higher static branch density correlates with
*fewer* instructions per byte (denser trace encoding), which matches the
intuition that more branches per instruction means more atom/address
packets needed. Presented as an illustrative small-N relationship, not an
inferential regression, per the plan's own stated framing — six points, real
scatter, and W3's static branch density (0.136) is notably lower than its
name ("branch-dense") suggests, because bubble sort's inner comparison
loop dominates *dynamically* (how often it runs) but not *statically* (how
many distinct branch instructions exist in the function body) — the two
are genuinely different quantities, and this metric measures the latter.

## Caveats, stated honestly

- **Branch density here is objdump-based, not sourced from the engine's
  own `ProgramDisassembler`.** The plan called for computing it from
  `ProgramDisassembler` directly; that would require a new CLI surface on
  `trailer-trace-replay` that doesn't exist yet. The objdump-based method
  is transparent and reasonable for an illustrative correlation, but it is
  a different code path from the one this project ships, and its branch
  classification (mnemonic pattern + "pc" in the operand text) is a
  simpler heuristic than whatever `ProgramDisassembler` uses internally
  for call/return/indirect-jump classification.
- **Static branch density is not the same thing as dynamic (execution-
  weighted) branch density.** W3's bubble sort is branch-*dominant* in
  execution because its inner loop runs many times, not because its
  function body contains an unusually large fraction of branch
  instructions — the static metric above does not capture this
  distinction, and the workload's design name should not be read as a
  claim about the static number.
- **N=6 workloads, N=6 repeats per workload.** Both counts are small by
  design (per the plan's own instruction not to oversell this as a
  powered study) — the density means and the branch-density correlation
  are illustrative evidence, not statistically powerful results.
