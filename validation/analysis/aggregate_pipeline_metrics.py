#!/usr/bin/env python3
"""Phase 9.4: aggregates trailer-trace-replay's --metrics sidecar across every
density-matrix capture (short/medium/long x rep0/rep1, all 6 workloads,
baseline ETM config only - the etm_sweep captures use different configs and
would confound a decode-time-vs-size comparison)."""

import json
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parents[2]
CAPTURES_ROOT = ENGINE_DIR / "validation" / "captures"
REPLAY = ENGINE_DIR / "build" / "trailer-trace-replay"
WORKLOADS = ["w1_baseline", "w2_callchain", "w3_branchdense", "w4_isr", "w5_dma", "w6_overflow"]
DURATIONS = ["short", "medium", "long"]
REPS = ["rep0", "rep1"]

METRIC_KEYS = [
    "bytesIn", "decodeMs", "precomputeMs", "transformInstructionsMs", "transformBlocksMs",
    "transformGapsMs", "traceRecords", "instructionsReconstructed", "resolveCalls",
]


def resolve_program(meta: dict) -> Path:
    program = Path(meta["program"])
    return program if program.is_absolute() else (ENGINE_DIR / program)


def run_one(capture_dir: Path) -> dict:
    meta = json.loads((capture_dir / "meta.json").read_text())
    program = resolve_program(meta)
    with tempfile.NamedTemporaryFile(suffix=".json") as tmp:
        subprocess.run(
            [str(REPLAY), "--elf", str(program), "--capture", str(capture_dir / "capture.bin"),
             "--format", "json", "--metrics", tmp.name],
            check=True, capture_output=True, text=True,
        )
        return json.loads(Path(tmp.name).read_text())


def main() -> int:
    rows = []
    for workload in WORKLOADS:
        for duration in DURATIONS:
            for rep in REPS:
                capture_dir = CAPTURES_ROOT / workload / duration / rep
                if not (capture_dir / "meta.json").exists():
                    continue
                metrics = run_one(capture_dir)
                row = {"workload": workload, "duration": duration, "rep": rep}
                row.update({k: metrics[k] for k in METRIC_KEYS})
                rows.append(row)

    out_csv = ENGINE_DIR / "validation" / "analysis" / "pipeline_metrics.csv"
    with open(out_csv, "w") as f:
        header = ["workload", "duration", "rep"] + METRIC_KEYS
        f.write(",".join(header) + "\n")
        for row in rows:
            f.write(",".join(str(row[k]) for k in header) + "\n")
    print(f"wrote {len(rows)} rows to {out_csv}")

    print(f"\naggregate over n={len(rows)} density-matrix captures (baseline ETM config):")
    for key in METRIC_KEYS:
        values = [row[key] for row in rows]
        mean = statistics.mean(values)
        stdev = statistics.stdev(values) if len(values) > 1 else 0.0
        print(f"  {key}: mean={mean:.4f} stdev={stdev:.4f} min={min(values):.4f} max={max(values):.4f}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
