# Finding 08 — Functional requirement coverage recap

Status: **synthesis of Findings 01-07 against the report's own FR/NFR
table (`chap_04.tex`), not new measurement.** Written to answer "how are
we validating each requirement" directly, and to make explicit which
requirements this campaign actually touches versus which remain validated
only by the chapter's existing (pre-campaign) evidence.

The report's own scope split (`chap_04.tex`): FR1-FR7 are the trace
pipeline and "hold the contribution of the project"; FR8-FR13 are
conventional debug operations present so trace joins a working debugger.
This campaign was scoped to the trace pipeline, so that split shows up
directly below — FR1-FR7/NFR1 get real new evidence, FR9/FR10 get
incidental reinforcement as a side effect of using them as infrastructure,
and FR8/FR11-13/NFR2-3 get none.

## FR1-FR7 and NFR1 — the trace pipeline (this campaign's focus)

| Req | Requirement | Existing chapter evidence | This campaign's new evidence |
|---|---|---|---|
| FR1 | Configure trace unit + sink over the debug connection | Register readback vs. saved reference | Every capture in Findings 04-07 (dozens of runs, 6 workloads, 4 ETM configs) re-exercises this configuration path end to end; the CTI experiments additionally exercise `cti create`/object-operation configuration, a path the chapter never used |
| FR2 | Start/stop trace in step with run state, no separate command | Two callbacks seen on every halt | Reconfirmed on every one of Finding 04's CTI-triggered self-halts: `tmc_stop_and_extract` fires automatically on `TARGET_EVENT_HALTED` with no explicit stop command sent, including from a hardware-triggered halt the chapter's own tests never produced |
| FR3 | Extract capture over the same debug connection, no extra hardware | "Every result in this chapter taken over the debug cable alone" | Same, now true across ~50+ real captures instead of one |
| FR4 | Decode byte stream into trace elements | Saved capture decoded against real unit config | Finding 01's element-kind coverage (19-member `ocsd_gen_trc_elem_t` enum, retained vs. dropped) now cross-checked against real captures with `Timestamp`/`CycleCount` elements actually present (Finding 06), closing 01's own deferred item |
| FR5 | Reconstruct executed instruction sequence | Hand-built fixtures + 3 vendor debuggers (comparison tools since unavailable) | **Now closed, see Finding 11.** An independent LLDB single-step shadow trace (400 instructions, W1) matched the hardware-trace reconstruction at 100.00% concordance (99.75% before a real defect it found was fixed — see below). |
| FR6 | Attribute instruction to function/file/line | 3 ELF fixtures | Real hardware attribution correct across every one of Findings 04/06/07's decodes (e.g. Finding 07's trace correctly names `Process`, `main`, `HardFault_Handler` by source line) — broader, real-hardware evidence, not a new correctness claim beyond what the fixtures already established |
| FR7 | Represent discontinuities explicitly, two sides kept distinct | Table 6.4: "0 discontinuities ever observed" — untested in practice | **Closes a real, previously-flagged gap.** Real `overflow` gaps were both produced and correctly counted/reported multiple times (W2's baseline captures: 19-20 overflow gaps; Finding 07's first, buggy capture: 43). Not yet done: confirming the pre-gap instruction prefix is CFG-valid (Phase 9.6) — gaps are shown to be *detected and reported correctly*, not yet shown to *bound a always-consistent prefix* |
| NFR1 | No hardware beyond the debug probe, no trace pins | Bench description | Same conclusion, now over dozens of runs including hardware-triggered CTI halts, not just standard capture |

## FR9/FR10 — incidental reinforcement, not this campaign's target

| Req | Requirement | This campaign's incidental evidence |
|---|---|---|
| FR9 | Run control primitives (halt, resume, step, breakpoints, watchpoints) | Breakpoint set/hit exercised heavily and successfully (Finding 05: ~100 hits; Finding 07: fault-entry breakpoint) — stepping and watchpoints not exercised at all here |
| FR10 | Read/write memory and registers, correct AP targeting | Heavily exercised (`DWT_CYCCNT`, `CFSR`/`HFSR`/`ICSR`, corrupted-pointer reads) — and **Finding 07 surfaces a real limitation the chapter's own FR10 validation doesn't cover**: on this Cortex-M7 with its D-cache enabled, a `readMemory` can return stale (pre-write) content for a value the CPU itself has already correctly written and acted on, because the debug port reads physical SRAM and bypasses the CPU's write-back cache. Verified with an A/B test (cache on vs. off), not asserted. Worth carrying into the report as a documented caveat on FR10, not just a Phase 7 side note. |

## Untouched by this campaign

FR8 (frontend presentation), FR11 (SVD/peripherals), FR12 (disassembly/variables), FR13 (reset/resync), NFR2 (Windows/Linux), NFR3 (protocol integration) — no board time was spent on any of these; they remain validated exactly as the existing chapter states, neither reinforced nor challenged here.

## What this means for the open campaign work

- **FR5 is now closed** (Finding 11) — the item the report's reviewer specifically flagged (the vendor-tool comparison "no longer available") has its replacement evidence.
- FR7's gap-handling now has real positive evidence behind it for the first time; finishing Phase 9.6 (CFG-conformance check across every capture's gap boundary) would close it completely. **Update: Phase 9.6 is now done — see Finding 12 (229,424 transitions checked, 99.993% clean). FR7 is fully closed, not just reinforced.**
- The FR10 cache-staleness finding is real, verified, and currently only written up as part of Finding 07 — it should be called out on its own terms in any final report text about FR10, not buried inside the fault case study.
- Finding 11 also surfaced a real, reportable pipeline defect not tied to any single FR: not-taken conditional branches were silently excluded from the reconstructed instruction listing (ARM's own "N atom" convention, confirmed from OpenCSD source). **Fixed** — the engine now includes them — worth a mention wherever the report discusses FR5/FR6's instruction listing, alongside the earlier-closed IT-block question.
