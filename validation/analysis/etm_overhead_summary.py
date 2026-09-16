#!/usr/bin/env python3
"""Fresh-decodes every etm_sweep_cti capture and writes a CSV - the source of
truth for Phase 11's ETM-overhead figure. Reuses verify_etm_sweep_results.py's
decode() so the numbers are pulled from the actual capture files on disk, not
transcribed from a finding (Finding 06's timestamp_cyclecount column was
retracted in Finding 14 for exactly that kind of drift)."""

import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from verify_etm_sweep_results import CAPTURES_ROOT, CONFIGS, WORKLOADS, decode  # noqa: E402

ENGINE_DIR = Path(__file__).resolve().parents[2]


def main() -> int:
    rows = []
    for workload in WORKLOADS:
        elf = ENGINE_DIR / "validation" / "firmware" / workload / "build" / f"{workload}.elf"
        for config in CONFIGS:
            capture = CAPTURES_ROOT / workload / "etm_sweep_cti" / config / "capture.bin"
            if not capture.exists():
                continue
            metrics = decode(elf, capture)
            rows.append({
                "workload": workload,
                "config": config,
                "instructions_reconstructed": metrics["instructionsReconstructed"],
                "overflow_gaps": metrics.get("gapsByReason", {}).get("overflow", 0),
            })

    out_csv = ENGINE_DIR / "validation" / "analysis" / "etm_overhead_summary.csv"
    with open(out_csv, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} rows to {out_csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
