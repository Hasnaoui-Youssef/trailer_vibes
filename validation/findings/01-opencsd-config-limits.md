# Finding 01 — OpenCSD ETMv4 config-compatibility limits

Status: **1.1 and the static part of 1.2 complete (no board needed). The
"rich capture" element-frequency cross-check in 1.2 is deferred until Phase
3 produces real captures with timestamp/cycle-count enabled.**

## 1.1 — Config-limit regression guard

`trailer-trace-replay` checks `TRCCONFIGR.COND` and `TRCCONFIGR.INSTP0`
before decoding (see `CheckConfigLimits` in `src/trailer_trace_replay.cc`),
failing immediately and readably instead of letting OpenCSD's decode throw
an opaque `ocsdError`. Verified against every register snapshot in the
tree today:

| Snapshot | COND | INSTP0 | Result |
|---|---|---|---|
| `test_resources/etm_regs.json` | 0 | 0 | pass |
| `test_resources/utils/etm_regs.json` (unconfigured/idle dump) | 0 | 0 | pass |
| Hardcoded default in `trailer-trace-replay` (matches `etm_dump.bin`'s capture conditions) | 0 | 0 | pass |

No existing capture or fixture in the tree ever trips this. This matches
the source-level finding (see the plan's Context section, and
[[reference_opencsd_etmv4_config_limits]] in project memory): this
project's own ETMv4 driver never exposes a way to set `TRCCONFIGR.COND`,
so the OpenCSD hard-rejection for conditional instruction tracing is
already structurally unreachable, not merely untested. `INSTP0` is
reachable in principle (`-data-addr`/`-data-value`) but is never turned on
by this campaign — see Phase 4's explicit exclusion.

This check now runs on every `trailer-trace-replay` invocation
unconditionally (not just under a flag), so every future capture in this
campaign is covered automatically.

## 1.2 — Element-kind coverage

Ground truth for the full `ocsd_gen_trc_elem_t` enum (19 members) came from
the vendored OpenCSD v1.8.3 source directly
(`build/_deps/opencsd-src/decoder/include/opencsd/trc_gen_elem_types.h`),
not recollection.

Static part, run now against the one committed capture
(`test_resources/etm_dump.bin`, baseline config, no timestamp/cycle-count/
retstack enabled):

```
retained:
  InstructionRange: 31
  Exception: 1
  TraceOn: 1
  NoSync: 2
dropped:
  EndOfTrace: 1
  PeContext: 1
```

Observations:
- No `Timestamp` or `CycleCount` elements appear, as expected — this
  capture predates any feature sweep and was taken at baseline
  configuration. Confirming these elements actually appear once Phase 4's
  rich-configuration captures exist is the deferred part of this finding.
- `PeContext` appears exactly once. M-profile devices carry no VMID/ASID,
  so this is very likely a single PE-state announcement at the start of
  the stream rather than anything context-switch-related — worth a
  one-line confirmation once more captures exist, not a concern now.
- `EndOfTrace` appears exactly once, at the end of the buffer, as
  expected.

Decision, from this evidence: `TraceRecordSink`/`TraceRecord` should gain
`Timestamp` and `CycleCount` retention ahead of Phase 9.5 (trace-derived
timing) — deferred until a rich capture actually exists to develop against,
since there's nothing to decode yet on this fixture.

## Incidental finding: the "118 vs 114" instruction count

`trailer_trace_decoder_output.log` (the pre-existing golden reference for
the retired CLI) ends with `35 trace records, 118 reconstructed
instructions`, while the current frozen-oracle test
(`trace_provider_test`) and this tool both report **114 executed
instructions** on the same capture. This is not a regression: the retired
CLI's trailing line counted `Transform<ReconstructedInstruction>`'s full
output (114 executed + 4 trailing not-executed range-enders = 118), while
the current pipeline's own counters (and this tool's default listing)
report the executed-only count. Both counts are internally consistent;
they're just two different quantities. Confirmed by direct inspection, not
assumed.

**Closed, not just explained**: Finding 11 later fixed the pipeline to
stop dropping not-taken instructions from its own listing. Both
`trace_provider_test` and this tool now report 118 on this same capture —
the "two different quantities" above are the same quantity today.

## Incidental finding: redundant `resolve()` calls are live in production, not just a test artifact

Instrumenting the `resolve` callable in `trailer-trace-replay` (counts
every call) on `etm_dump.bin`: **464 resolve() calls for 114 executed
instructions** (~4.07x), decomposing exactly as:

- `Transform<ReconstructedInstruction>` over all ranges (executed +
  not-executed range-enders): 118 calls.
- `Transform<FunctionBlock>` internally re-runs
  `Transform<ReconstructedInstruction>` from scratch: another 118 calls.
- `Transform<FunctionBlock>` re-resolves every *executed* instruction a
  second time for its own location lookup: 114 calls.
- This tool's own text-mode printing loop re-resolves every executed
  instruction again: 114 calls.

118+118+114+114 = 464, exactly.

The first two terms are not specific to this tool — `trace_session.cc`'s
`TraceSession::Append` (used by `core::TraceManager` in production) calls
`Transform<ReconstructedInstruction>` and `Transform<FunctionBlock>`
independently on every capture, the same redundant pattern. This
corroborates Phase 9.4's planned double-resolve finding with a live,
reproducible number rather than a code-reading inference, and shows it's a
real production cost (paid on every `DecodeWorkerMain` capture), not a
test-only artifact. **Fixed in Finding 13** — the double-walk this
section identifies is the exact defect that finding measures before/after
and removes.
