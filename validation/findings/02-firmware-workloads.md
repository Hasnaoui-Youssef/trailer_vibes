# Finding 02 — Firmware workload suite

Status: **built and compile-verified. Functional behavior on real hardware
(does the TIM6 ISR actually fire at the expected rate, does the DMA
transfer complete, does the fault actually trigger) is confirmed in Phase
3, not assumed here** — this note covers what can honestly be established
without the board.

## Build infrastructure

All eight images share one Makefile fragment
(`validation/firmware/common/Makefile.common`) and one clock configuration
(`validation/firmware/common/Core/Src/board_init.c`, byte-identical PLL/bus
tree to `test_resources/stm32h7s3x_dummy`), so every workload runs at the
same core frequency and is built with the same flags
(`-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -Og -g -gdwarf-5`,
`arm-none-eabi-gcc` 14.2.1). HAL driver sources are shared by reference from
the existing `test_resources/stm32h7s3x_dummy/Drivers` tree rather than
duplicated; TIM HAL sources (absent from that vendored subset) were added
once, in `validation/firmware/common/hal_extra/`, sourced from the full
STM32Cube_FW_H7RS_V1.3.0 pack.

**Sanity check on the shared infrastructure**: W1 (byte-for-byte the same
control flow as the existing dummy project) builds to
`text=4832 data=12 bss=1568`, matching the dummy project's own known-good
build exactly. This confirms the refactored shared Makefile reproduces the
original build byte-for-byte before any workload-specific content is
trusted.

## Size budget (64 KB internal flash)

| Workload | text | data | bss | Notes |
|---|---:|---:|---:|---|
| W1 baseline | 4832 | 12 | 1568 | = existing dummy project |
| W2 call chain | 4908 | 12 | 1568 | |
| W3 branch-dense | 4924 | 12 | 1600 | |
| W4 TIM6 ISR | 6044 | 12 | 1648 | pulls in HAL TIM driver, `--gc-sections` keeps it small |
| W5 DMA | 6916 | 12 | 1952 | pulls in HAL DMA/HPDMA + callback registration |
| W6 overflow | 4832 | 12 | 1568 | identical shape to W1, deliberately |
| W7 fault | 4888 | 12 | 1580 | |
| gfxmmu_trigger | 4808 | 12 | 1568 | |

All comfortably under the 64 KB budget (largest is 8880 bytes total, ~14%).

## W7 verified at the disassembly level before flashing

The fault case study (Phase 7) depends on `Process()`'s off-by-one loop
surviving `-Og` compilation exactly as written — an out-of-bounds array
write is undefined behavior in the C standard, and a more aggressive
optimizer could in principle eliminate or reorder it. Disassembled
(`arm-none-eabi-objdump -d`) rather than assumed:

```
800034c: movs r3, #0                    ; i = 0
8000350: sdiv r2, r3, r1                 ; i % len
         mls  r2, r1, r2, r3
         ldrb.w ip, [r0, r2]             ; ip = data[i % len]
         ldr  r2, [pc, #12]              ; r2 = &record.buffer[0] (0x24000028)
         strb.w ip, [r2, r3]             ; record.buffer[i] = data[i % len]
         adds r3, #1
8000364: cmp r3, r1
         ble.n 8000350                   ; loop while i <= len  (9 writes into an 8-byte buffer)
```

Confirms the loop runs `i = 0..8` inclusive (`ble`, not `blt`) against an
8-byte buffer — the 9th write is the intended one-byte overflow, landing on
`on_complete`'s low byte at `record`'s base address `0x24000028` (AXI
SRAM, as expected). The corrupting byte is `payload[0] = 0x00`, which
clears bit 0 of the pointer; branching to a non-Thumb address via an
indirect call is an architecturally guaranteed UsageFault (INVSTATE) on
Cortex-M, so the fault does not depend on what happens to be stored at the
corrupted target address. Verified in the compiled binary, not assumed
from the source alone.

## Design notes worth recording

- W1 is deliberately identical in control flow to the existing
  `test_resources/stm32h7s3x_dummy` capture behind the report's Table 6.4 —
  this workload's Phase 3 results are directly comparable to that existing
  single data point, bridging the old and new evidence.
- W6 reuses W1's exact structure's spirit (a tight decision-per-iteration
  loop) but is a distinct, even more branch-dense pattern chosen to fill
  the 2 KB TMC buffer as fast as possible once the capture window is held
  open past its capacity.
- W4's ISR-dependent branch (`isr_tick_count & 1u`) means the main loop's
  own control flow is not fully static — the trace should show it tracking
  real interrupt timing, which is itself a small, free piece of evidence
  for Phase 5/9.5's work once real captures exist.
