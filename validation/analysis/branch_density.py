#!/usr/bin/env python3
"""Static branch density per workload, restricted to its own functions (objdump-based, not ProgramDisassembler)."""

import re
import subprocess
import sys

WORKLOAD_FUNCTIONS = {
    "w1_baseline": ["main"],
    "w2_callchain": ["main", "level1", "level2", "level3", "level4"],
    "w3_branchdense": ["main", "BubbleSort"],
    "w4_isr": ["main", "MX_TIM6_Init", "HAL_TIM_PeriodElapsedCallback"],
    "w5_dma": ["main", "MX_HPDMA1_Init", "TransferComplete", "TransferError"],
    "w6_overflow": ["main"],
}

BRANCH_MNEMONICS = re.compile(
    r"^(b|bl|blx|bx|beq|bne|bcc|blo|bcs|bhs|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble|cbz|cbnz|"
    r"it|itt|ite|ittt|itte|itet|itee)\.?w?$"
)


def disassemble_function(elf_path: str, func: str) -> list:
    out = subprocess.run(
        ["arm-none-eabi-objdump", "-d", f"--disassemble={func}", elf_path],
        capture_output=True, text=True, check=True,
    ).stdout
    instructions = []
    for line in out.splitlines():
        m = re.match(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2,4}\s+)+(\S+)\s*(.*)", line)
        if m:
            instructions.append((m.group(1), m.group(2)))
    return instructions


def branch_density(elf_path: str, funcs: list) -> tuple:
    total = 0
    branches = 0
    for func in funcs:
        for mnem, operands in disassemble_function(elf_path, func):
            total += 1
            is_branch = bool(BRANCH_MNEMONICS.match(mnem)) or re.search(r"\bpc\b", operands) is not None
            if is_branch:
                branches += 1
    return branches, total


def main() -> int:
    for workload, funcs in WORKLOAD_FUNCTIONS.items():
        elf_path = f"validation/firmware/{workload}/build/{workload}.elf"
        branches, total = branch_density(elf_path, funcs)
        density = branches / total if total else 0.0
        print(f"{workload}: {branches}/{total} branch-classified instructions ({density:.3f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
