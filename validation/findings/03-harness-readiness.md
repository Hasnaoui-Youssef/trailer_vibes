# Finding 03 — Campaign harness readiness

Status: **written and unit-verified without hardware; execution needs the
board.**

## What's verified without the board

- `validation/campaign/dap_client.py`'s `Client`/framing logic runs
  end-to-end against the real `build/trailer-dap` binary: `initialize`
  succeeds with `supportsTraceRequests: true`, and `trailerTraceStatus`
  (which needs no session) returns
  `{"available": false, "enabled": false, "reason": "unavailable", ...}` —
  matching the exact wire shape confirmed from source
  (`TraceStatusResponseBody`'s `ToString(TraceStatusReason)`).
- Every DAP field name used by the six campaign scripts (`readMemory`,
  `writeMemory`, `setBreakpoints`, `evaluate`, `trailerTrace*`) was
  confirmed against the actual protocol structs and their `fromJSON`/
  `toJSON` mappings, not assumed from the DAP spec alone.
- `readMemory`/`writeMemory` on a `launch` session are confirmed to run
  through `OpenOcdMemoryStrategy` (installed by `TargetManager::Launch`
  immediately after the OpenOCD provider is created), which works while
  the target is running — this is what makes Phase 5's live cycle-count
  read (with no breakpoint anywhere in the measured region) actually
  sound, not just convenient.
- `openocd.rawCommands` are confirmed to run before adapter/transport
  init, but still well before `CreateTrace()` — so a `<sink> configure
  -output ...` command in `rawCommands` is guaranteed to be in effect
  before `trailerTraceEnable` is ever sent (Phase 3/4/7's raw-capture
  mirroring). The same finding rules out using `rawCommands` for Phase 8's
  post-hang AP0 probe (no adapter exists yet at that point in the launch
  sequence) — which is why that script opens its own independent telnet
  connection to OpenOCD instead, live, after the hang.
- W1's cycle-counter instrumentation (added to `main.c` for Phase 5)
  rebuilds cleanly (`text=4884`, +52 bytes over the pre-instrumentation
  build) and the `last_loop_cycles` symbol resolves via `arm-none-eabi-nm`
  as expected.

## Scripts, one per phase

| Script | Phase | Needs the board |
|---|---|---|
| `run_capture.py` | 3 (acquisition matrix) | yes |
| `run_etm_sweep_cti.py` | 4 (ETM feature sweep) | yes |
| `run_intrusiveness.py` | 5 (DWT cycle-counter) | yes |
| `run_tsan_stress.py` | 6 (concurrency stress) | yes, + `linux-tsan` build of `trailer-dap` |
| `run_fault_case_study.py` | 7 (fault case study) | yes |
| `run_ap_survivability.py` | 8 (dual-AP) | yes — **high risk, run last, manual, power-cycle required regardless of outcome** |

## What isn't verified yet, honestly

None of these six scripts have been run against real hardware — every
board-dependent line of protocol logic is correct against the source-level
contract, not against an observed response from a live session. The first
real board session (Phase 3) is also the first real end-to-end test of
`run_capture.py` itself; if something in the actual runtime behavior
differs from what the source review predicted (timing, an unexpected event
ordering, a slower-than-expected `pause` response), the script's timeouts
and assumptions will need adjusting against real data, not assumed correct
in advance.
