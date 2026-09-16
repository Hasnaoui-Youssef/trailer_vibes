#!/usr/bin/env python3
"""Isolates the TRCCONFIGR register-passing fix's effect on the
timestamp_cyclecount capture for each workload: decodes today's on-disk
capture.bin twice with today's code, once with the decoder's disabled
default register fallback and once with the real committed TRCCONFIGR,
so the comparison holds the code and the bytes fixed and only varies the
register snapshot. See validation/findings/14-*.md and its correction."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parents[2]
CAPTURES_ROOT = ENGINE_DIR / "validation" / "captures"
REPLAY = ENGINE_DIR / "build" / "trailer-trace-replay"
ETM_REGS = Path(__file__).resolve().parent / "etm_regs_timestamp_cyclecount.json"
WORKLOADS = ["w1_baseline", "w2_callchain", "w3_branchdense", "w4_isr", "w5_dma", "w6_overflow"]


def decode(elf: Path, capture: Path, etm_regs: Path | None) -> dict:
    with tempfile.NamedTemporaryFile(suffix=".json") as tmp:
        args = [str(REPLAY), "--elf", str(elf), "--capture", str(capture), "--format", "json", "--metrics", tmp.name]
        if etm_regs is not None:
            args += ["--etm-regs", str(etm_regs)]
        subprocess.run(args, check=True, capture_output=True, text=True)
        return json.loads(Path(tmp.name).read_text())


def main() -> int:
    print(f"{'workload':16} {'regs':10} {'instructions':>13} {'noSyncGaps':>11} {'overflowGaps':>13}")
    for workload in WORKLOADS:
        elf = CAPTURES_ROOT.parent / "firmware" / workload / "build" / f"{workload}.elf"
        capture = CAPTURES_ROOT / workload / "etm_sweep_cti" / "timestamp_cyclecount" / "capture.bin"
        if not capture.exists():
            print(f"{workload:16} (missing capture)")
            continue
        for label, regs in (("default", None), ("corrected", ETM_REGS)):
            metrics = decode(elf, capture, regs)
            gaps = metrics.get("gapsByReason", {})
            print(f"{workload:16} {label:10} {metrics['instructionsReconstructed']:>13} "
                  f"{gaps.get('noSync', 0):>11} {gaps.get('overflow', 0):>13}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
