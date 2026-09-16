#!/usr/bin/env python3
"""Phase 5: CTI-triggered halt-on-buffer-full, run once per workload, N repeats each."""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cti_trigger import cleanup, raw_commands  # noqa: E402
from dap_client import disconnect, spawn, launch, tid_from_threads  # noqa: E402
from run_cti_experiment import run_experiment  # noqa: E402
from flash_target import flash  # noqa: E402

WORKLOADS = {
    "w1_baseline": "validation/firmware/w1_baseline/build/w1_baseline.elf",
    "w2_callchain": "validation/firmware/w2_callchain/build/w2_callchain.elf",
    "w3_branchdense": "validation/firmware/w3_branchdense/build/w3_branchdense.elf",
    "w4_isr": "validation/firmware/w4_isr/build/w4_isr.elf",
    "w5_dma": "validation/firmware/w5_dma/build/w5_dma.elf",
    "w6_overflow": "validation/firmware/w6_overflow/build/w6_overflow.elf",
}


class _Args:
    pass


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("adapter_path")
    p.add_argument("repo_root")
    p.add_argument("tcl_dir")
    p.add_argument("board_cfg")
    p.add_argument("output_dir")
    p.add_argument("--repeat", type=int, default=5)
    p.add_argument("--gdb-port", default="3333")
    p.add_argument("--telnet-port", type=int, default=4444)
    return p.parse_args()


def run_workload(name: str, program_path: str, parsed, out_dir: Path) -> bool:
    print(f"=== {name} ===")
    if not flash(program_path):
        print(f"FAILED: flashing {program_path} did not succeed", file=sys.stderr)
        return False

    client = spawn(parsed.adapter_path)
    args = _Args()
    args.telnet_port = parsed.telnet_port
    args.repeat = parsed.repeat

    ok, launch_resp, config_resp = launch(
        client, program_path, parsed.tcl_dir, parsed.board_cfg, gdb_port=parsed.gdb_port,
        raw_commands=raw_commands(), extra_openocd={"telnetPort": str(parsed.telnet_port)},
        stop_on_entry=True, timeout_s=30.0)
    if not ok:
        print(f"FAILED: launch did not succeed: launch={launch_resp} configurationDone={config_resp}",
              file=sys.stderr)
        return False

    try:
        results = run_experiment(client, tid_from_threads(client), args)
    finally:
        cleanup(parsed.telnet_port)
        disconnect(client)
        client.proc.terminate()

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "results.json").write_text(json.dumps({"program": program_path, "repeats": results}, indent=2))

    halted = [r for r in results if r["halted"]]
    print(f"-> {name}: {len(halted)}/{len(results)} clean halts")
    return bool(halted)


def main() -> int:
    parsed = parse_args()
    repo_root = Path(parsed.repo_root)
    base_dir = Path(parsed.output_dir)
    ok_all = True
    for name, rel_elf in WORKLOADS.items():
        program_path = str(repo_root / rel_elf)
        ok_all = run_workload(name, program_path, parsed, base_dir / name / "fill_time") and ok_all
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
