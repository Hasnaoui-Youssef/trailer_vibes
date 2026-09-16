#!/usr/bin/env python3
"""Recomputes Finding 10's density-study numbers directly from the campaign's
own capture files and branch_density.py, and writes a CSV - the source of
truth for Phase 11's figures, rather than transcribing numbers out of a
markdown table."""

import csv
import json
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from branch_density import branch_density, WORKLOAD_FUNCTIONS  # noqa: E402

ENGINE_DIR = Path(__file__).resolve().parents[2]
CAPTURES_ROOT = ENGINE_DIR / "validation" / "captures"
REPLAY = ENGINE_DIR / "build" / "trailer-trace-replay"
WORKLOADS = ["w1_baseline", "w2_callchain", "w3_branchdense", "w4_isr", "w5_dma", "w6_overflow"]
DURATIONS = ["short", "medium", "long"]
REPS = ["rep0", "rep1"]


def instructions_reconstructed(elf: Path, capture: Path) -> int:
    with tempfile.NamedTemporaryFile(suffix=".json") as tmp:
        subprocess.run(
            [str(REPLAY), "--elf", str(elf), "--capture", str(capture), "--format", "json", "--metrics", tmp.name],
            check=True, capture_output=True, text=True,
        )
        return json.loads(Path(tmp.name).read_text())["instructionsReconstructed"]


def main() -> int:
    rows = []
    for workload in WORKLOADS:
        elf = ENGINE_DIR / "validation" / "firmware" / workload / "build" / f"{workload}.elf"
        densities = []
        for duration in DURATIONS:
            for rep in REPS:
                capture = CAPTURES_ROOT / workload / duration / rep / "capture.bin"
                if not capture.exists():
                    continue
                instr = instructions_reconstructed(elf, capture)
                densities.append(instr / 2064.0)

        branches, total = branch_density(str(elf), WORKLOAD_FUNCTIONS[workload])
        rows.append({
            "workload": workload,
            "mean_instr_per_byte": statistics.mean(densities),
            "stdev_instr_per_byte": statistics.stdev(densities) if len(densities) > 1 else 0.0,
            "min_instr_per_byte": min(densities),
            "max_instr_per_byte": max(densities),
            "n": len(densities),
            "branch_density": branches / total if total else 0.0,
        })

    out_csv = ENGINE_DIR / "validation" / "analysis" / "density_summary.csv"
    with open(out_csv, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} rows to {out_csv}")
    for row in sorted(rows, key=lambda r: r["mean_instr_per_byte"]):
        print(f"  {row['workload']:16} mean={row['mean_instr_per_byte']:.3f} "
              f"stdev={row['stdev_instr_per_byte']:.3f} branch_density={row['branch_density']:.3f} n={row['n']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
