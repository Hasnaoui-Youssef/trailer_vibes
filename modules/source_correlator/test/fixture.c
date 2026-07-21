// Source for fixture.elf, the SourceCorrelator unit test's firmware image.
// Deliberately tiny but forces inlining (add_one/double_it into compute)
// so the fixture actually exercises the full inline-frame-chain case, not
// just plain non-inlined line resolution.
//
// Rebuild with (arm-none-eabi-gcc, e.g. from an Arm GNU Toolchain), from
// this directory so the DWARF-embedded compilation-unit name stays the
// portable relative "fixture.c" rather than a build-machine-specific
// absolute path:
//   arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -g -Og -ffreestanding \
//       -nostdlib -fno-inline-small-functions \
//       -Wl,--section-start=.text=0x08000000 -o fixture.elf fixture.c
//
// Expected disassembly + line correlation
// (arm-none-eabi-objdump -dl fixture.elf):
//   08000000 <compute>:
//    8000000: adds r0, #1          ; add_one(), fixture.c:2  (inlined; call
//                                   ; site fixture.c:10 in compute)
//    8000002: add.w r1, r1, r1, lsl #1   ; double_it(), fixture.c:6 (inlined;
//                                          call site fixture.c:11 in compute)
//    8000006: add r0, r1           ; compute(), fixture.c:13 (not inlined)
//    8000008: bx lr
//   0800000a <_start>: ...

__attribute__((always_inline)) static inline int add_one(int x) {
    return x + 1;
}

__attribute__((always_inline)) static inline int double_it(int x) {
    return x * 3;
}

int compute(int a, int b) {
    int r1 = add_one(a);
    int r2 = double_it(b);
    return r1 + r2;
}

void _start(void) {
    volatile int result = compute(3, 4);
    while (1) {
    }
}
