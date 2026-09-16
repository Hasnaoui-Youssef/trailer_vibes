# Finding 15 — Exception attribution (Phase 9.7)

Status: **built, unit-tested, and validated against real captures across
the whole campaign.** A new module, `trace_exception_attribution`, maps
each `ExceptionEvent`'s raw `exception_number` to a named vector-table
entry via the engine's existing SVD/device model (`device_xml::Device`) —
the connection between the trace and device-modeling subsystems the plan
asked for.

## Method

`trace::ExceptionName(exception_number, device)` resolves a number to a
name in two steps: first checks whether it falls in one of ETMv4's own
M-class peripheral-IRQ encodings (IRQ0–IRQ7 at a `+16` offset from the
core exceptions, IRQ8 and above at a separate `+0x200` encoding — both
per OpenCSD's own `trc_pkt_elem_etmv4i.cpp`, the pipeline's authoritative
decoder) and looks up a match by IRQ number against every peripheral's
declared `<interrupt>` in the SVD; if that fails, falls back to the fixed
32-entry M-class core/pseudo-exception table (Reset, HardFault, ...,
SysTick, DebugHalt, Lockup, ...). `trace::AttributeExceptions` applies
this across a full `ExceptionEvent` stream, leaving return events
unresolved (their `exception_number` is architecturally 0 and would
otherwise misleadingly resolve to "Reserved"). Wired into `trailer-trace-
replay` behind a new `--svd PATH` flag; unaffected output when omitted
(confirmed unchanged against the existing regression fixture).

## A real bug caught by testing against real hardware, not assumed

The first implementation assumed the simple architectural IPSR extension
(`IRQn = exception_number - 16` for every external interrupt), which
resolved exception 24 to "IRQ8" and, by coincidence, matched a real SVD
peripheral (FLASH, whose IRQ is genuinely 8) — a plausible-looking wrong
answer that would have passed a shallow check. Cross-checking against
OpenCSD's own `MExcep[]` table (the code actually decoding this
project's trace data) showed exception 24 is not IRQ8 at all: it is
`DebugHalt`, a Cortex-M debug pseudo-exception, and IRQ8 and every IRQ
above it are encoded at a completely different offset (`exception_number
- 0x200`, not `-16`). Fixed to match OpenCSD's own table exactly, with a
unit test asserting the values that would have silently passed under the
wrong formula.

## Real-hardware validation

Every existing capture in the campaign (36 across all six workloads) was
decoded with `--svd test_resources/xml/stm32h7s.svd`:

| Resolved name | Count | Path exercised |
|---|---:|---|
| SysTick (exception 15) | 2 | fixed core-exception table |
| DebugHalt (exception 24) | 36 | fixed core-exception table (pseudo-exception) |

Both are real, physically-explicable results: SysTick fires in every
workload (`HAL_Init`, per Finding 12), and DebugHalt is exactly what
ETMv4 reports at the debug-request halt every one of these captures ends
on to drain the ETF buffer — not a placeholder or an artifact, a genuine
architectural signal this pipeline had never surfaced before.

No capture in the campaign happens to have a live peripheral IRQ (e.g.
W4's own `TIM6`) as its single visible exception event — plausible given
each capture holds only a 2 KB window and, per Finding 14, most gap/
exception slots are consumed by debug-halt or resync events rather than
workload-driven ones. The peripheral-IRQ lookup path itself (both the
IRQ0–7 and IRQ8+ encodings, and the SVD name match) is verified
separately and exactly by `exception_attribution_test` against a
synthetic device declaring `TIM6` at its real SVD IRQ number (55) — a
deterministic, no-hardware check that does not depend on one turning up
live in a capture.

## Validated against Phase 7's fault case study

Re-decoding Finding 07's own capture (`validation/captures/w7_fault/capture.bin`)
with `--svd`:

```
before executed-instruction index 2944: exception entry, number 3 (HardFault)
before executed-instruction index 2944: exception entry, number 24 (DebugHalt)
```

Exception 3 names correctly as `HardFault` — precisely the fault W7's
corrupted-function-pointer design is meant to trigger, named directly
rather than left as a bare address jump, exactly the concrete improvement
the plan asked this transform to demonstrate.

## Caveats, stated honestly

- **No campaign capture happens to exercise the peripheral-IRQ lookup
  path against a real, named peripheral interrupt** — the unit test
  covers it exactly; production hardware data confirms the two other
  paths (core exceptions, pseudo-exceptions), and now the fault-exception
  path, but not this third one.
