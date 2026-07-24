@ Source for fixture.elf, the ProgramDisassembler unit test's firmware
@ image. Deliberately tiny: covers a plain instruction, a direct call
@ (bl), and a return (bx lr), which is enough to exercise DisassembleRange
@ and its control-flow classification without needing a real firmware
@ build.
@
@ Rebuild with (arm-none-eabi binutils, e.g. from an Arm GNU Toolchain):
@   arm-none-eabi-as -mcpu=cortex-m7 -mthumb fixture.s -o fixture.o
@   arm-none-eabi-ld -T fixture.ld -o fixture.elf fixture.o
@
@ Expected disassembly (arm-none-eabi-objdump -d fixture.elf):
@   08000000 <_start>:
@    8000000: b004        add  sp, #16
@    8000002: f000 f802   bl   800000a <target>
@    8000006: 4770        bx   lr
@    8000008: bf00        nop
@   0800000a <target>:
@    800000a: bf00        nop

.syntax unified
.thumb
.text
.global _start
_start:
    add sp, sp, #16
    bl  target
    bx  lr
    nop
target:
    nop
