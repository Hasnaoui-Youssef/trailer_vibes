#!/usr/bin/env python3
"""Expand engine/resources/XML/Devices/*.xml into resources/device_index.tsv.

Run manually and commit the output; Devices/ itself (130MB of files we read
four fields out of) is not committed. Re-run only when the CubeMX database
in resources/XML/Devices/ is refreshed.

    python3 tools/expand_devices.py
"""

import os
import re
import sys

DEVICES_DIR = os.path.join(os.path.dirname(__file__), "..", "resources", "XML", "Devices")
CORES_DIR = os.path.join(os.path.dirname(__file__), "..", "resources", "XML", "Cores")
OUT_PATH = os.path.join(os.path.dirname(__file__), "..", "resources", "device_index.tsv")

PAREN_RE = re.compile(r"^(.*)\(([^)]*)\)(.*)\.xml$")
REFNAME_RE = re.compile(r'RefName="([^"]*)"')
CORE_RE = re.compile(r"<Core>([^<]*)</Core>")
DIE_RE = re.compile(r"<Die>([^<]*)</Die>")
RAM_RE = re.compile(r"<Ram>([^<]*)</Ram>")
FLASH_RE = re.compile(r"<Flash>([^<]*)</Flash>")


def map_core(raw, core_files):
    s = raw.strip()
    if s.startswith("ARM "):
        s = s[4:]
    elif s.startswith("Arm "):
        s = s[4:]
    s = s.replace("Cortex-M0+", "Cortex-M0plus")
    return s if s in core_files else ""


def main():
    core_files = {f[:-4] for f in os.listdir(CORES_DIR)}
    rows = []
    anomalies = []

    for fname in sorted(os.listdir(DEVICES_DIR)):
        if not fname.endswith(".xml"):
            continue
        text = open(os.path.join(DEVICES_DIR, fname), encoding="utf-8", errors="replace").read(20000)

        refname_match = REFNAME_RE.search(text)
        if not refname_match:
            anomalies.append(f"{fname}: no RefName")
            continue

        die_match = DIE_RE.search(text)
        if not die_match:
            anomalies.append(f"{fname}: no Die")
            continue
        die = die_match.group(1)

        raw_cores = CORE_RE.findall(text)
        if not raw_cores:
            anomalies.append(f"{fname}: no Core")
            continue
        # Multi-core devices (e.g. M7+M4) list the debug-primary core first;
        # the secondary core's SVD isn't tracked here.
        core = map_core(raw_cores[0], core_files)
        core_count = len(raw_cores)

        ram_tags = RAM_RE.findall(text)
        flash_tags = FLASH_RE.findall(text)
        if not ram_tags or not flash_tags:
            anomalies.append(f"{fname}: missing Ram/Flash")
            continue

        paren = PAREN_RE.match(fname)
        if not paren:
            if len(flash_tags) != 1 or len(ram_tags) != 1:
                anomalies.append(f"{fname}: no paren group but multiple Ram/Flash tags")
                continue
            rows.append((refname_match.group(1), core, die, ram_tags[0], flash_tags[0], core_count))
            continue

        prefix, opts_str, suffix = paren.groups()
        opts = opts_str.split("-")
        if len(flash_tags) != len(opts):
            anomalies.append(f"{fname}: #Flash={len(flash_tags)} != #opts={len(opts)}")
            continue
        if len(ram_tags) not in (1, len(opts)):
            anomalies.append(f"{fname}: #Ram={len(ram_tags)} matches neither 1 nor #opts={len(opts)}")
            continue

        for i, opt in enumerate(opts):
            device_name = f"{prefix}{opt}{suffix}"
            ram = ram_tags[0] if len(ram_tags) == 1 else ram_tags[i]
            rows.append((device_name, core, die, ram, flash_tags[i], core_count))

    rows.sort(key=lambda r: r[0])

    with open(OUT_PATH, "w", encoding="utf-8") as out:
        out.write("device_name\tcore\tdie\tram_kb\tflash_kb\tcore_count\n")
        for row in rows:
            out.write("\t".join(str(v) for v in row) + "\n")

    print(f"wrote {len(rows)} rows to {OUT_PATH}")
    if anomalies:
        print(f"{len(anomalies)} anomalies:", file=sys.stderr)
        for a in anomalies:
            print(f"  {a}", file=sys.stderr)


if __name__ == "__main__":
    main()
