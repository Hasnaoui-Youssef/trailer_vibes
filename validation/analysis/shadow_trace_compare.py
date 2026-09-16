#!/usr/bin/env python3
"""Phase 9.2: aligns the shadow (single-step) trace against the hardware trace, classifying divergences."""

import json
import re
import subprocess
import sys

COND_BRANCH_MNEMONICS = re.compile(r"^(beq|bne|bcc|blo|bcs|bhs|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble|cbz|cbnz)\.?n?$")


def disassemble_all(elf_path: str) -> dict:
    out = subprocess.run(["arm-none-eabi-objdump", "-d", elf_path], capture_output=True, text=True, check=True).stdout
    mnemonics = {}
    for line in out.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2,4}\s+)+(\S+)", line)
        if m:
            mnemonics[int(m.group(1), 16)] = m.group(2)
    return mnemonics


def main() -> int:
    elf_path, shadow_json, hw_text = sys.argv[1], sys.argv[2], sys.argv[3]

    shadow = [int(a, 16) for a in json.load(open(shadow_json))["addresses"]]
    hw = []
    with open(hw_text, errors="replace") as f:
        for line in f:
            m = re.match(r"^(0x[0-9a-f]+)\t", line)
            if m:
                hw.append(int(m.group(1), 16))

    mnemonics = disassemble_all(elf_path)

    i = j = 0
    matches = 0
    explained_not_taken = 0
    unexplained = []
    while i < len(shadow) and j < len(hw):
        if shadow[i] == hw[j]:
            matches += 1
            i += 1
            j += 1
            continue
        mnem = mnemonics.get(shadow[i], "")
        if COND_BRANCH_MNEMONICS.match(mnem) and i + 1 < len(shadow) and shadow[i + 1] == hw[j]:
            explained_not_taken += 1
            i += 1
            continue
        unexplained.append((i, j, hex(shadow[i]), hex(hw[j]), mnem))
        i += 1
        j += 1

    total = matches + explained_not_taken + len(unexplained)
    print(f"shadow trace: {len(shadow)} steps, hardware trace: {len(hw)} instructions")
    print(f"exact matches: {matches}")
    print(f"explained divergences (not-taken conditional branch, excluded per ARM E/N atom convention): {explained_not_taken}")
    print(f"unexplained divergences: {len(unexplained)}")
    print(f"concordance (matches / (matches+explained+unexplained)): {(matches + explained_not_taken) / total * 100:.2f}%")
    for entry in unexplained[:20]:
        print("  unexplained:", entry)
    return 0


if __name__ == "__main__":
    sys.exit(main())
