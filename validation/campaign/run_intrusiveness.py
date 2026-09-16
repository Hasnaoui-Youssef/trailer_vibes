#!/usr/bin/env python3
"""Phase 5: per-iteration CYCCNT delta at a repeated breakpoint, trace off vs on. W1-specific."""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dap_client import disconnect, ensure_dwt_cycle_counter_enabled, launch, read_memory_u32  # noqa: E402
from dap_client import spawn, DWT_CYCCNT  # noqa: E402
from run_cti_experiment import tid_from_threads  # noqa: E402
from flash_target import flash  # noqa: E402

LOOP_BODY_LINE = 19  # "int x = 0;", the first statement of every W1 while(1) iteration


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("adapter_path")
    p.add_argument("program_path")
    p.add_argument("tcl_dir")
    p.add_argument("board_cfg")
    p.add_argument("--samples", type=int, default=50, help="breakpoint hits per condition (yields samples-1 deltas)")
    p.add_argument("--gdb-port", default="3333")
    p.add_argument("--output-json", default=None)
    return p.parse_args()


def collect_deltas(client, tid: int, n: int) -> list:
    raw = []
    for _ in range(n):
        client.send("continue", {"threadId": tid})
        client.wait_for_response("continue", timeout_s=10.0)
        found = client.wait_for_all(
            {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=10.0)
        if "stopped" not in found:
            raise RuntimeError("never hit the loop-body breakpoint within 10s")
        cyccnt = read_memory_u32(client, DWT_CYCCNT)
        if cyccnt is None:
            raise RuntimeError("readMemory(DWT_CYCCNT) failed at a breakpoint stop")
        raw.append(cyccnt)
    return [b - a for a, b in zip(raw, raw[1:])]


def summarize(label: str, deltas: list) -> dict:
    s = sorted(deltas)
    stats = {"n": len(deltas), "min": s[0], "median": s[len(s) // 2], "max": s[-1],
              "mean": sum(deltas) / len(deltas)}
    print(f"-> {label}: {stats}")
    return stats


def main() -> int:
    args = parse_args()
    main_c = (Path(args.program_path).resolve().parent.parent / "Core" / "Src" / "main.c")

    print(f"-> flashing {args.program_path}")
    if not flash(args.program_path):
        print("FAILED: flashing did not succeed", file=sys.stderr)
        return 1

    client = spawn(args.adapter_path)
    ok, launch_resp, config_resp = launch(
        client, args.program_path, args.tcl_dir, args.board_cfg, gdb_port=args.gdb_port,
        stop_on_entry=True, timeout_s=30.0)
    if not ok:
        print(f"FAILED: launch did not succeed: launch={launch_resp} configurationDone={config_resp}",
              file=sys.stderr)
        return 1

    try:
        if not ensure_dwt_cycle_counter_enabled(client):
            print("FAILED: could not enable DWT cycle counter", file=sys.stderr)
            return 1

        client.send("setBreakpoints", {"source": {"path": str(main_c)}, "breakpoints": [{"line": LOOP_BODY_LINE}]})
        bp_resp = client.wait_for_response("setBreakpoints", timeout_s=10.0)
        bps = (bp_resp.get("body") or {}).get("breakpoints", []) if bp_resp else []
        if not bps or not bps[0].get("verified"):
            print(f"FAILED: breakpoint at {main_c}:{LOOP_BODY_LINE} did not verify: {bp_resp}", file=sys.stderr)
            return 1

        tid = tid_from_threads(client)

        print(f"-> sampling {args.samples} breakpoint hits, trace disabled")
        deltas_disabled = collect_deltas(client, tid, args.samples)

        print("-> trailerTraceEnable")
        client.send("trailerTraceEnable")
        print(f"   {client.wait_for_response('trailerTraceEnable', timeout_s=10.0)}")

        print(f"-> sampling {args.samples} breakpoint hits, trace enabled")
        deltas_enabled = collect_deltas(client, tid, args.samples)

        client.send("trailerTraceDisable")
        client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
    finally:
        disconnect(client)
        client.proc.terminate()

    stats_disabled = summarize("trace disabled", deltas_disabled)
    stats_enabled = summarize("trace enabled", deltas_enabled)
    print(f"-> delta (median-to-median): {stats_enabled['median'] - stats_disabled['median']} cycles")

    if args.output_json:
        Path(args.output_json).write_text(json.dumps({
            "program": args.program_path,
            "breakpoint": f"{main_c}:{LOOP_BODY_LINE}",
            "trace_disabled": {"deltas": deltas_disabled, "stats": stats_disabled},
            "trace_enabled": {"deltas": deltas_enabled, "stats": stats_enabled},
        }, indent=2))

    return 0


if __name__ == "__main__":
    sys.exit(main())
