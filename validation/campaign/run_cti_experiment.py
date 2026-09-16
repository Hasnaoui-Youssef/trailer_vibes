#!/usr/bin/env python3
"""CTI experiment: checks for a real halt on buffer-full and reports the DWT cycle delta."""

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cti_trigger import SYS_CTI, M7_CTI, ack, arm, cleanup, raw_commands  # noqa: E402
from dap_client import disconnect, ensure_dwt_cycle_counter_enabled, launch, read_memory_u32  # noqa: E402
from dap_client import spawn, tid_from_threads, DWT_CYCCNT  # noqa: E402
from flash_target import flash  # noqa: E402


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("adapter_path")
    p.add_argument("program_path")
    p.add_argument("tcl_dir")
    p.add_argument("board_cfg")
    p.add_argument("--telnet-port", type=int, default=4444)
    p.add_argument("--gdb-port", default="3333")
    p.add_argument("--repeat", type=int, default=5)
    p.add_argument("--output-json", default=None)
    return p.parse_args()


def main() -> int:
    args = parse_args()

    print(f"-> flashing {args.program_path}")
    if not flash(args.program_path):
        print("FAILED: flashing did not succeed", file=sys.stderr)
        return 1

    client = spawn(args.adapter_path)
    ok, launch_resp, config_resp = launch(
        client, args.program_path, args.tcl_dir, args.board_cfg, gdb_port=args.gdb_port,
        raw_commands=raw_commands(), extra_openocd={"telnetPort": str(args.telnet_port)},
        stop_on_entry=True, timeout_s=30.0)
    if not ok:
        print(f"FAILED: launch did not succeed: launch={launch_resp} configurationDone={config_resp}",
              file=sys.stderr)
        return 1

    try:
        results = run_experiment(client, tid_from_threads(client), args)
    finally:
        cleanup(args.telnet_port)
        disconnect(client)
        client.proc.terminate()

    if args.output_json:
        Path(args.output_json).write_text(json.dumps({"program": args.program_path, "repeats": results}, indent=2))

    halted = [r for r in results if r["halted"]]
    return 0 if halted else 1


def measure_once(client, tid: int, port: int) -> dict:
    marks = {"start": time.monotonic()}

    ack(SYS_CTI, port)
    ack(M7_CTI, port)
    marks["acked"] = time.monotonic()

    client.send("trailerTraceDisable")
    disable_resp = client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
    marks["disabled"] = time.monotonic()
    client.send("trailerTraceEnable")
    enable_resp = client.wait_for_response("trailerTraceEnable", timeout_s=10.0)
    marks["enabled"] = time.monotonic()

    t0 = read_memory_u32(client, DWT_CYCCNT)
    marks["t0_read"] = time.monotonic()
    client.send("continue", {"threadId": tid})
    continue_resp = client.wait_for_response("continue", timeout_s=10.0)
    marks["continue_acked"] = time.monotonic()

    found = client.wait_for_all(
        {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=10.0)
    marks["stopped"] = time.monotonic()
    wall = {
        "ack_s": marks["acked"] - marks["start"],
        "disable_s": marks["disabled"] - marks["acked"],
        "enable_s": marks["enabled"] - marks["disabled"],
        "t0_read_s": marks["t0_read"] - marks["enabled"],
        "continue_ack_s": marks["continue_acked"] - marks["t0_read"],
        "halt_wait_s": marks["stopped"] - marks["continue_acked"],
        "total_s": marks["stopped"] - marks["start"],
        "disable_timed_out": disable_resp is None,
        "enable_timed_out": enable_resp is None,
        "continue_timed_out": continue_resp is None,
    }
    if "stopped" not in found:
        return {"halted": False, "t0": t0, "t1": None, "delta": None, "wall": wall}

    t1 = read_memory_u32(client, DWT_CYCCNT)
    delta = t1 - t0 if t0 is not None and t1 is not None else None
    return {"halted": True, "t0": t0, "t1": t1, "delta": delta, "wall": wall}


def run_experiment(client, tid: int, args) -> list:
    if not ensure_dwt_cycle_counter_enabled(client):
        print("FAILED: could not enable DWT cycle counter", file=sys.stderr)
        return []

    arm(args.telnet_port)

    results = []
    for i in range(args.repeat):
        r = measure_once(client, tid, args.telnet_port)
        print(f"-> rep{i}: {r}")
        results.append(r)
    return results


if __name__ == "__main__":
    sys.exit(main())
