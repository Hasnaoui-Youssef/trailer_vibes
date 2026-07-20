#!/usr/bin/env python3
"""
Read a binary dump of registers (32‑bit words) and export as JSON.

Usage:
    python reg_dump_to_json.py <dump.bin>

The script expects a binary file where each register value occupies 4 bytes.
Offsets in the dictionary are in bytes (must be 4‑byte aligned).
The output is written to ./etm_regs.json as a single JSON object.
"""

import struct
import json
import sys

# ----------------------------------------------------------------------
# Fill this dictionary yourself:  key = register name, value = byte offset
# Example:
#   register_offsets = {
#       "ETMCR": 0x000,
#       "ETMSR": 0x004,
#       "ETMTEEVR": 0x020,
#       ...
#   }
# ----------------------------------------------------------------------
register_offsets = {
    # "REG_NAME": offset_in_bytes,
    "TRCCONFIGR"    : 0x010,
    "TRCTRACEIDR"   : 0x040,
    "TRCIDR8"       : 0x180,
    "TRCIDR9"       : 0x184,
    "TRCIDR10"      : 0x188,
    "TRCIDR11"      : 0x18C,
    "TRCIDR12"      : 0x190,
    "TRCIDR13"      : 0x194,
    "TRCIDR0"       : 0x1E0,
    "TRCIDR1"       : 0x1E4,
    "TRCIDR2"       : 0x1E8,
    "TRCIDR3"       : 0x1EC,
    "TRCIDR4"       : 0x1F0,
    "TRCIDR5"       : 0x1F4,
    "TRCIDR6"       : 0x1F8,
    "TRCIDR7"       : 0x1FC,
    "TRCAUTHSTATUS" : 0xFB8,
}

def read_register_dump(file_path):
    """Parse binary dump and return dict of {register_name: value}."""
    with open(file_path, 'rb') as f:
        raw = f.read()

    # Interpret as 32-bit unsigned integers
    word_count = len(raw) // 4
    if len(raw) % 4 != 0:
        print("Warning: file size not a multiple of 4; trailing bytes ignored.",
              file=sys.stderr)

    fmt = f"<{word_count}I"
    words = struct.unpack(fmt, raw[:word_count * 4])

    result = {}
    for name, offset in register_offsets.items():
        if offset % 4 != 0:
            print(f"Warning: offset {offset:#x} not 4‑byte aligned, skipping '{name}'.",
                  file=sys.stderr)
            continue
        index = offset // 4
        if 0 <= index < word_count:
            result[name] = words[index]
        else:
            print(f"Warning: offset {offset:#x} out of range for register '{name}'.",
                  file=sys.stderr)

    return result


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)

    input_file = sys.argv[1]
    regs = read_register_dump(input_file)

    output_path = "./etm_regs.json"
    with open(output_path, 'w') as f:
        json.dump(regs, f, indent=2)

    print(f"Successfully wrote {len(regs)} register values to {output_path}")


if __name__ == "__main__":
    main()
