#!/usr/bin/env python3
"""Phase 9.6: checks every consecutive instruction transition in a decode against the static CFG."""

import re
import sys

COND_BRANCH_MNEMONICS = re.compile(r"^(beq|bne|bcc|blo|bcs|bhs|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble|cbz|cbnz)(\.[nw])?$")
CALL_MNEMONICS = re.compile(r"^bl\.?w?$|^blx\.?w?$")
BRANCH_OR_CALL_MNEMONICS = re.compile(r"^(b|bl|blx|bx)\.?w?n?$")
BARE_REGISTER_OPERAND = re.compile(r"^(r\d{1,2}|lr|pc|sp)$", re.IGNORECASE)

def parse_decode(text_path: str) -> tuple:
    instructions = []
    discontinuity_before_index = set()  # gaps and exception entry/return both break normal CFG transitions
    with open(text_path, errors="replace") as f:
        for line in f:
            m = re.match(r"^\s*before executed-instruction index (\d+):", line)
            if m:
                discontinuity_before_index.add(int(m.group(1)))
                continue
            if not line.startswith("0x"):
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) < 5:
                continue
            addr = int(fields[0], 16)
            size = len(fields[1].split())
            mnem = fields[2]
            operands = fields[3]
            branch_target = None
            is_return = False
            is_indirect = False
            if len(fields) == 6:
                flag = fields[4]
                if flag.startswith("-> "):
                    branch_target = int(flag[3:], 16)
                elif flag == "<return>":
                    is_return = True
                elif flag == "<indirect>":
                    is_indirect = True
            if BRANCH_OR_CALL_MNEMONICS.match(mnem) and BARE_REGISTER_OPERAND.match(operands.strip()):
                is_indirect = True  # a register-operand branch/call has no statically-known target
            instructions.append((addr, size, mnem, branch_target, is_return, is_indirect))
    return instructions, discontinuity_before_index


def executed_index_before(instructions: list) -> list:
    """Maps each raw list position to the engine's own executed-instruction
    count immediately before it - a not-taken conditional branch now occupies
    its own row but, like the engine's gap/exception indexing, does not
    advance that count."""
    result = [0] * (len(instructions) + 1)
    for i, (addr, size, mnem, branch_target, is_return, is_indirect) in enumerate(instructions):
        not_taken = False
        if i + 1 < len(instructions) and COND_BRANCH_MNEMONICS.match(mnem) and branch_target is not None:
            next_addr = instructions[i + 1][0]
            not_taken = next_addr == addr + size and next_addr != branch_target
        result[i + 1] = result[i] + (0 if not_taken else 1)
    return result


def check(text_path: str) -> tuple:
    instructions, discontinuity_before_index = parse_decode(text_path)
    exec_index = executed_index_before(instructions)

    checked = 0
    skipped_at_discontinuity = 0
    violations = []
    call_stack = []

    for i in range(len(instructions) - 1):
        if exec_index[i + 1] in discontinuity_before_index:
            call_stack.clear()  # a gap or exception invalidates any pending call/return expectation
            skipped_at_discontinuity += 1
            continue

        addr, size, mnem, branch_target, is_return, is_indirect = instructions[i]
        next_addr = instructions[i + 1][0]
        checked += 1

        if CALL_MNEMONICS.match(mnem):
            call_stack.append(addr + size)

        fallthrough = next_addr == addr + size
        taken = branch_target is not None and next_addr == branch_target
        returned = is_return and call_stack and next_addr == call_stack[-1]
        if returned:
            call_stack.pop()
        elif is_return and not call_stack:
            returned = True  # return to outside this window's call-stack view - can't assert, don't flag

        if not (fallthrough or taken or returned or is_indirect):
            violations.append((i, hex(addr), mnem, hex(next_addr)))

    return checked, violations, skipped_at_discontinuity


def main() -> int:
    text_path = sys.argv[1]
    checked, violations, skipped = check(text_path)
    print(f"{text_path}: {checked} transitions checked ({skipped} skipped at gap/exception boundaries), {len(violations)} violations")
    for v in violations[:20]:
        print("  violation:", v)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
