#!/usr/bin/env python3
"""Phase 9.6: decodes every campaign capture to text and runs cfg_conformance.py
against each, aggregating totals. Replaces an earlier, unpersisted ad-hoc run -
this is the reproducible version, with its own explicit, stated scope."""

import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cfg_conformance import check  # noqa: E402

ENGINE_DIR = Path(__file__).resolve().parents[2]
CAPTURES_ROOT = ENGINE_DIR / "validation" / "captures"
REPLAY = ENGINE_DIR / "build" / "trailer-trace-replay"
WORKLOADS = ["w1_baseline", "w2_callchain", "w3_branchdense", "w4_isr", "w5_dma", "w6_overflow"]
DURATIONS = ["short", "medium", "long"]
REPS = ["rep0", "rep1"]
ETM_CONFIGS = ["baseline", "retstack", "timestamp_cyclecount", "branch_broadcast"]


def elf_for(workload: str) -> Path:
    return ENGINE_DIR / "validation" / "firmware" / workload / "build" / f"{workload}.elf"


def decode_to_text(elf: Path, capture: Path) -> Path:
    tmp = tempfile.NamedTemporaryFile(suffix=".txt", delete=False, mode="w")
    tmp.close()
    subprocess.run(
        [str(REPLAY), "--elf", str(elf), "--capture", str(capture), "--format", "text"],
        check=True, stdout=open(tmp.name, "w"), stderr=subprocess.DEVNULL,
    )
    return Path(tmp.name)


def main() -> int:
    captures = []
    for workload in WORKLOADS:
        for duration in DURATIONS:
            for rep in REPS:
                captures.append((workload, CAPTURES_ROOT / workload / duration / rep / "capture.bin"))
        for config in ETM_CONFIGS:
            captures.append((workload, CAPTURES_ROOT / workload / "etm_sweep_cti" / config / "capture.bin"))
    captures.append(("w7_fault", CAPTURES_ROOT / "w7_fault" / "capture.bin"))

    total_checked = 0
    total_skipped = 0
    all_violations = []
    n_captures = 0
    for workload, capture in captures:
        if not capture.exists() or capture.suffix != ".bin":
            continue
        elf = elf_for(workload)
        text_path = decode_to_text(elf, capture)
        try:
            checked, violations, skipped = check(str(text_path))
        finally:
            text_path.unlink()
        n_captures += 1
        total_checked += checked
        total_skipped += skipped
        for v in violations:
            all_violations.append((str(capture.relative_to(ENGINE_DIR)), v))

    print(f"captures checked: {n_captures}")
    print(f"transitions checked: {total_checked}")
    print(f"skipped at gap/exception boundaries: {total_skipped}")
    print(f"clean: {total_checked - len(all_violations)} "
          f"({(total_checked - len(all_violations)) / total_checked * 100:.3f}%)")
    print(f"violations: {len(all_violations)}")
    for capture, v in all_violations:
        print(f"  {capture}: {v}")
    return 1 if all_violations else 0


if __name__ == "__main__":
    sys.exit(main())
