# Finding 16 — ThreadSanitizer concurrency stress (Phase 6)

Status: **run against real hardware under `linux-tsan`, one real finding
caught and reproduced 3/3 attempts, run duration bounded by memory rather
than by the stress design itself — documented honestly below, including
a real incident this session caused and mitigated.**

## A real incident: the first attempt crashed the development machine

The first `run_tsan_stress.py` run (default settings: an unthrottled
inspection loop firing `stackTrace`/`evaluate` as fast as the socket would
accept them, no memory ceiling) crashed the host machine outright — this
system has no swap, so once the TSan-instrumented `trailer-dap` (LLDB +
OpenOCD + TSan shadow memory, all in one process) exhausted RAM, the
kernel OOM killer had no cushion and the crash took down more than the
offending process. All subsequent runs were wrapped in
`systemd-run --user --scope -p MemoryMax=4G -p MemorySwapMax=0`, giving
the kernel a hard, cgroup-scoped ceiling, plus `TSAN_OPTIONS=hard_rss_limit_mb=3500`
so TSan's own runtime self-reports before that harder cap needs to act.
Every run after this point stayed fully contained to its own cgroup, with
zero impact on the rest of the machine, confirmed via `journalctl`.

Two bugs in the script itself were also found and fixed while getting a
clean run: `-etf`/`-etm configure` were passed as pre-`init` `rawCommands`
(every other campaign script correctly issues them post-launch via
`telnet_command`, matching this driver's own requirement that `configure`
run after `init`), and `trailerWatchStart` was called with a `count`
argument where the real API expects `size`.

## Real memory behavior under this configuration

| Run | Inspection rate | Duration attempted | Outcome |
|---|---|---:|---|
| 1 (unthrottled, uncapped) | as fast as possible | 300s | crashed the host machine |
| 2 (unthrottled, capped) | as fast as possible | 20s | TSan self-reported `hard rss limit exhausted (3500Mb vs 3576Mb)` within ~20s |
| 3 (throttled to 50 Hz, capped) | 1 request/20ms | 150s | ran essentially the full window (149 of 150s, per `journalctl`'s start/kill timestamps) before the cgroup's OOM killer ended it |

Throttling the inspection loop from unbounded to 50 Hz bought roughly a
7x longer safe run (20s → 149s) under the same 4 GB ceiling, but did not
eliminate the growth — a TSan-instrumented build embedding all of LLDB
plus a live OpenOCD session appears to accumulate shadow-memory and/or
per-request state fast enough that a multi-minute run at any meaningfully
active request rate needs either a taller memory ceiling than this
machine can safely give it (no swap) or a shorter window. Not
root-caused further here (would need a non-TSan RSS profile of the same
workload to separate "real per-cycle growth" from "TSan's own well-known
shadow-memory overhead," out of scope for this pass).

## The finding: a reproducible lock-order-inversion at LLDB debugger initialization

Caught identically on all 3 runs, always during `TargetManager::InitializeDebugger()`
(never from the later stress loops themselves):

```
WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock)
  Mutex M1 acquired here while holding mutex M0 in thread T2 ('trailer-dap.evt'):
    ... liblldb-22.so.1 ...
    ... core::ExecutionController's thread entry point ...
  Mutex M0 acquired here while holding mutex M1 in main thread:
    ... liblldb-22.so.1 ...
    ... __libc_start_call_main
```

Thread T2 is confirmed (by matching the reported `void
(core::ExecutionController::*)()` thread-invocation signature) to be
`ExecutionController::EventThreadMain` - checked directly against source:
every LLDB call in that function (`debugger.GetListener()`, two
`AddListener` calls, `StartListeningForEventClass`, `WaitForEvent`) is a
direct, unguarded SB API call - none of them go through this project's
own `WithTarget()` mutex. Both M0 and M1 are therefore internal to LLDB
itself, not this project's own locks - a different pair from the one
`docs/architecture/05-known-characteristics.md` already documents
(`ExecutionController::HandleTargetEvent`'s `WithTarget` + `modules_mutex`
convention-only ordering, which involves two of *our* locks, not LLDB's).

The main thread's side of the report shows zero frames from this
project's own code - the acquisition happens entirely inside
`liblldb-22.so.1`, immediately below `__libc_start_call_main`, consistent
with a one-time LLDB-internal initialization path (plausibly triggered by
the first `SBDebugger::Create()` call in `InitializeDebugger()`) that
would, by construction, always complete on the main thread before
`StartEventThread()` later creates T2 - which would make the two
acquisition orders never actually concurrent in practice, regardless of
what TSan's static lock-order graph reports. This could not be confirmed
further: `liblldb-22.so.1` on this system is a fully stripped release
build (`nm -D` resolves only exported symbols like `PyInit__lldb`), so
the exact LLDB-internal functions and mutexes involved aren't resolvable
without a symbol-ful LLDB build, which isn't available here.

## Disposition

- **Real, 3/3 reproducible finding**, but its actual severity is
  unresolved: plausibly benign (a one-time, single-threaded
  initialization order that can never race against the later multi-
  threaded phase), but not confirmed against LLDB's own source, since
  the linked build is stripped. Documented here rather than dismissed or
  overclaimed.
- **No other race was found** despite ~149 seconds of concurrent
  `continue`/`pause`/`cancel`/`stackTrace`/`evaluate`/`threads` cycling
  plus a live trace capture and memory watch running throughout - the
  two gaps `05-known-characteristics.md` names (unlocked
  `GetLLDBThread`/`GetLLDBFrame`; `HandleTargetEvent`'s convention-only
  mutex ordering) were exercised by this load (both `stackTrace` and
  `evaluate` drive exactly that unlocked path) without TSan reporting
  either as an actual race in this run.
- **Memory, not the stress design itself, is what bounds run length** on
  this machine for this configuration - a real, load-bearing constraint
  for anyone reproducing this: never run `run_tsan_stress.py` without a
  memory ceiling, given the demonstrated ability to take the whole host
  down on a swapless machine.
