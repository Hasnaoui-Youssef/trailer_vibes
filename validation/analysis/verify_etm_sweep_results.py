#!/usr/bin/env python3
"""Re-verifies Finding 06's instructions-reconstructed table by decoding the
actual etm_sweep_cti capture files currently on disk, independent of
whatever produced the numbers written into the finding."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parents[2]
CAPTURES_ROOT = ENGINE_DIR / "validation" / "captures"
REPLAY = ENGINE_DIR / "build" / "trailer-trace-replay"
WORKLOADS = ["w1_baseline", "w2_callchain", "w3_branchdense", "w4_isr", "w5_dma", "w6_overflow"]
CONFIGS = ["baseline", "retstack", "timestamp_cyclecount", "branch_broadcast"]

# baseline/retstack/branch_broadcast decode identically with or without a
# live register snapshot (Finding 14); only timestamp_cyclecount needs the
# real TRCCONFIGR (cci/ts bits) instead of the decoder's disabled-by-default
# fallback, so only that config gets one here.
ETM_REGS = {
    "timestamp_cyclecount": Path(__file__).resolve().parent / "etm_regs_timestamp_cyclecount.json",
}

# Finding 06's published table, for direct comparison.
PUBLISHED = {
    ("w1_baseline", "baseline"): 4433, ("w1_baseline", "retstack"): 4433,
    ("w1_baseline", "timestamp_cyclecount"): 2062, ("w1_baseline", "branch_broadcast"): 4433,
    ("w2_callchain", "baseline"): 1513, ("w2_callchain", "retstack"): 1513,
    ("w2_callchain", "timestamp_cyclecount"): 839, ("w2_callchain", "branch_broadcast"): 1513,
    ("w3_branchdense", "baseline"): 6224, ("w3_branchdense", "retstack"): 6224,
    ("w3_branchdense", "timestamp_cyclecount"): 2438, ("w3_branchdense", "branch_broadcast"): 6224,
    ("w4_isr", "baseline"): 3337, ("w4_isr", "retstack"): 3240,
    ("w4_isr", "timestamp_cyclecount"): 1108, ("w4_isr", "branch_broadcast"): 3337,
    ("w5_dma", "baseline"): 3347, ("w5_dma", "retstack"): 3347,
    ("w5_dma", "timestamp_cyclecount"): 1627, ("w5_dma", "branch_broadcast"): 3347,
    ("w6_overflow", "baseline"): 7321, ("w6_overflow", "retstack"): 7321,
    ("w6_overflow", "timestamp_cyclecount"): 2197, ("w6_overflow", "branch_broadcast"): 7321,
}


def decode(elf: Path, capture: Path, etm_regs: Path | None) -> dict:
    with tempfile.NamedTemporaryFile(suffix=".json") as tmp:
        args = [str(REPLAY), "--elf", str(elf), "--capture", str(capture), "--format", "json", "--metrics", tmp.name]
        if etm_regs is not None:
            args += ["--etm-regs", str(etm_regs)]
        subprocess.run(args, check=True, capture_output=True, text=True)
        return json.loads(Path(tmp.name).read_text())


def main() -> int:
    print(f"{'workload':16} {'config':22} {'published':>10} {'actual':>10} {'match':>6} {'overflowGaps':>13}")
    mismatches = 0
    for workload in WORKLOADS:
        elf = CAPTURES_ROOT.parent / "firmware" / workload / "build" / f"{workload}.elf"
        for config in CONFIGS:
            capture = CAPTURES_ROOT / workload / "etm_sweep_cti" / config / "capture.bin"
            if not capture.exists():
                print(f"{workload:16} {config:22} {'(missing capture)':>10}")
                continue
            metrics = decode(elf, capture, ETM_REGS.get(config))
            actual = metrics["instructionsReconstructed"]
            published = PUBLISHED[(workload, config)]
            match = "OK" if actual == published else "DIFF"
            if match == "DIFF":
                mismatches += 1
            overflow = metrics.get("gapsByReason", {}).get("overflow", 0)
            print(f"{workload:16} {config:22} {published:>10} {actual:>10} {match:>6} {overflow:>13}")
    print(f"\n{mismatches} mismatches out of {len(WORKLOADS) * len(CONFIGS)} cells")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
