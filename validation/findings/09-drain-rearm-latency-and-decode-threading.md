# Finding 09 — Drain/re-arm engine latency, and decode threading

Status: **measured on real hardware (W1, n=20 clean repeats) plus source
verification.** New investigation, not in the original plan: the CTI
halt-on-full mechanism from Finding 04 can be reused as a general
"deterministic full-coverage" tracing technique — halt on full, drain,
resume, repeat — but that reintroduces real intrusiveness on the *engine*
side (not the target side). This measures that cost, and answers two
concrete architecture questions about the decode path. All timing here is
host wall-clock measuring the *engine's own* behavior, not MCU execution —
consistent with this campaign's standing rule against inferring MCU timing
from the host clock.

## A real bug found first: the campaign's own telnet helper never detected "done"

Before any of this could be measured honestly, a bug in
`validation/campaign/dap_client.py`'s `telnet_command()` had to be found
and fixed. It read from the socket until either the overall deadline
passed or an individual `recv()` timed out — but OpenOCD's telnet server
keeps the connection open after answering, so the *second* `recv()` call
always blocked for the full 5 s timeout before giving up, regardless of
how fast the actual response arrived. Every one of Finding 04's ~20 s
per-repeat figures was this bug (4 telnet round trips in `ack()`, each
paying the full 5 s), not an engine cost — see Finding 04's corrected
caveat.

Fixed by switching to an idle-timeout read: the first `recv()` keeps the
full timeout (to tolerate a genuinely slow response), every subsequent
`recv()` gets a short 0.2 s idle window, so the read returns as soon as
the socket goes quiet instead of waiting out the original fixed timeout.
Verified before and after on the identical sequence: per-repeat wall time
dropped from ~20.1 s to ~0.9 s (a ~20x reduction), while the real,
on-target `CYCCNT` deltas were bit-for-bit unchanged — confirming this was
purely a host-side script defect with no bearing on any measurement
that used the real DAP protocol channel (a separate code path from the
raw telnet calls).

## Drain/re-arm latency, measured cleanly (W1, n=20)

Per-phase wall-clock breakdown of one full halt→drain→re-arm→resume
cycle, instrumented directly in `measure_once()`:

| Phase | median | min | max | What it is |
|---|---:|---:|---:|---|
| `disable_s` | 2.22 ms | 0.48 ms | 660.56 ms (1 outlier) | `trailerTraceDisable` DAP round trip (drain) |
| `enable_s` | 2.92 ms | 1.15 ms | 13.64 ms | `trailerTraceEnable` DAP round trip (re-arm) |
| `halt_wait_s` | 84.55 ms | 53.02 ms | 128.64 ms | time from `continue` to the `stopped` event |
| `total_s` (excl. CTI-specific `ack_s`) | ~90 ms | — | — | disable + enable + halt-wait |

(`ack_s`, ~805 ms median, is excluded from the "engine cost" figure above —
it is 4 raw telnet round trips specific to *this experiment's* CTI-arming
protocol, not something a normal trace session ever does.)

**The dominant cost is `halt_wait_s`, and it is not decode, not
extraction, and not the target — it is OpenOCD's own polling interval.**
`TARGET_DEFAULT_POLLING_INTERVAL` and the server's own `polling_period`
are both hardcoded to 100 ms (`external/openocd/src/target/target.h`,
`external/openocd/src/server/server.c`). The actual on-target fill time
for this halt was ~7347 DWT cycles ≈ 76.5 µs at 96 MHz — roughly
**1000x faster** than the ~85 ms it took the engine to notice and report
the halt. The 53–129 ms spread matches a halt landing at a random point
within one ~100 ms poll cycle almost exactly: notice a halt right after a
poll and wait nearly the full period; notice one right before a poll and
it's reported almost immediately.

**Practical consequence for the "full coverage via repeated CTI-triggered
drains" idea**: each refill cycle costs on the order of ~90 ms of engine
overhead (dominated by poll latency) to service what is, on-target, a
sub-100-microsecond capture window. Using this mechanism for full code
coverage is real and buildable, but it is overwhelmingly the *polling
architecture*, not the trace pipeline itself, that would set the pace —
roughly 10 coverage-refill cycles per second of wall time, regardless of
workload.

## Does decode need to run on every drain? Does it block resume?

Traced through `modules/core/src/components/trace_manager.cc` directly,
not assumed:

- `TraceManager::Create()` subscribes a callback (`OnCapture`) with
  OpenOCD's trace sink. `OnCapture()` runs on every drain (every barrier
  or capture), but it only appends the raw bytes to `pending_`/
  `decode_queue_` under a mutex and notifies a condition variable —
  no decoding happens on this path.
- **Decoding already runs on its own dedicated thread.** The constructor
  starts `decode_worker_ = std::thread(&TraceManager::DecodeWorkerMain, this)`,
  and `DecodeWorkerMain` is the only place that calls `session_.Append()`
  (the actual decode + instruction/function-block transform). It waits on
  the condition variable and processes whatever `OnCapture` enqueued,
  entirely decoupled from the thread that services DAP requests and
  target events.
- **This means resume is never blocked on decode.** The engine can, in
  principle, re-arm and resume before a previous chunk's decode has even
  started, since `OnCapture` never waits on `DecodeWorkerMain`.

## How long does decode actually take, and would a separate thread help?

Decode time for a 2 KB capture is already measured, repeatedly, across
this campaign via `trailer-trace-replay --metrics` — the same
`TraceSession::Append` code path `DecodeWorkerMain` calls in production
(confirmed in Finding 01). Representative figures already collected this
session: `decodeMs` 0.85–1.9 ms, `transformBlocksMs` 1.2–15.1 ms,
`transformInstructionsMs` 0.7–8.1 ms — decode plus transform for a full
2 KB buffer costs **single-digit to low-double-digit milliseconds**, not
measured fresh for this finding but drawn directly from data already on
disk from Findings 01, 04, 06, and 07's own decode runs.

**Answer: no measurable benefit from a separate decode thread, because
one already exists and decode was never the bottleneck.** At ~1–15 ms,
decode is an order of magnitude smaller than the ~85 ms polling-dominated
halt-detection latency above, and it already runs off the critical path.
The real cost of a repeated-drain "full coverage" scheme is the engine's
100 ms target-poll interval, not anything about decoding or its
threading.

## Caveats, stated honestly

- **n=20, one workload (W1).** The `disable_s`/`enable_s`/`halt_wait_s`
  figures are architecture-level costs (DAP round trip, OpenOCD polling)
  that should not depend on which workload is running — but only one was
  measured here.
- **One `disable_s` outlier (660.56 ms) is unexplained.** Out of 20
  repeats, one `trailerTraceDisable` call took two orders of magnitude
  longer than the rest. Not chased further here; worth watching for if
  this mechanism is used for anything latency-sensitive.
- **Decode timing is drawn from offline `trailer-trace-replay` runs, not
  instrumented freshly inside a live `DecodeWorkerMain` for this finding.**
  The code path is confirmed identical (Finding 01), so this is a
  well-founded estimate, not a direct in-process measurement.
