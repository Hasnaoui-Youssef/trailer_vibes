#!/usr/bin/env python3
"""Flashes an ELF via the system OpenOCD, standalone, before any trailer-dap session exists."""

import subprocess
import sys

SYSTEM_OPENOCD = "/usr/local/bin/openocd"


def flash(elf_path: str) -> bool:
    cmd = [
        SYSTEM_OPENOCD,
        "-f", "interface/stlink-dap.cfg",
        "-c", "transport select dapdirect_swd",
        "-f", "target/stm32h7rx.cfg",
        "-c", "init",
        "-c", "reset halt",
        "-c", f"flash write_image erase {elf_path}",
        "-c", f"verify_image {elf_path}",
        "-c", "reset halt",
        "-c", "shutdown",
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    output = result.stdout + result.stderr
    ok = "wrote " in output and "verified " in output and "Error" not in output
    if not ok:
        print(output, file=sys.stderr)
    return ok


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <path-to.elf>", file=sys.stderr)
        return 2
    ok = flash(sys.argv[1])
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
