#!/usr/bin/env python3
"""Phase 9.2: single-step W1 at instruction granularity, recording PC after each step."""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dap_client import disconnect, launch, spawn, tid_from_threads  # noqa: E402
from flash_target import flash  # noqa: E402

STEADY_STATE_LINE = 19


def current_pc(client, tid: int) -> int | None:
    client.send("stackTrace", {"threadId": tid})
    resp = client.wait_for_response("stackTrace", timeout_s=10.0)
    frames = (resp.get("body") or {}).get("stackFrames", []) if resp else []
    if not frames:
        return None
    ref = frames[0].get("instructionPointerReference")
    return int(ref, 16) if ref else None


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("adapter_path")
    p.add_argument("program_path")
    p.add_argument("tcl_dir")
    p.add_argument("board_cfg")
    p.add_argument("output_json")
    p.add_argument("--steps", type=int, default=400)
    p.add_argument("--gdb-port", default="3333")
    p.add_argument("--telnet-port", type=int, default=4444)
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
        extra_openocd={"telnetPort": str(args.telnet_port)}, stop_on_entry=True, timeout_s=30.0)
    if not ok:
        print(f"FAILED: launch did not succeed: launch={launch_resp} configurationDone={config_resp}",
              file=sys.stderr)
        return 1

    main_c = Path(args.program_path).resolve().parent.parent / "Core" / "Src" / "main.c"

    try:
        client.send("setBreakpoints", {"source": {"path": str(main_c)}, "breakpoints": [{"line": STEADY_STATE_LINE}]})
        bp_resp = client.wait_for_response("setBreakpoints", timeout_s=10.0)
        bps = (bp_resp.get("body") or {}).get("breakpoints", []) if bp_resp else []
        if not bps or not bps[0].get("verified"):
            print(f"FAILED: breakpoint at {main_c}:{STEADY_STATE_LINE} did not verify: {bp_resp}", file=sys.stderr)
            return 1

        tid = tid_from_threads(client)

        client.send("continue", {"threadId": tid})
        client.wait_for_response("continue", timeout_s=10.0)
        found = client.wait_for_all(
            {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=10.0)
        if "stopped" not in found:
            print("FAILED: never hit the steady-state breakpoint", file=sys.stderr)
            return 1

        client.send("setBreakpoints", {"source": {"path": str(main_c)}, "breakpoints": []})
        client.wait_for_response("setBreakpoints", timeout_s=10.0)

        addresses = []
        pc = current_pc(client, tid)
        if pc is None:
            print("FAILED: could not read initial PC", file=sys.stderr)
            return 1
        addresses.append(pc)

        for i in range(args.steps - 1):
            client.send("next", {"threadId": tid, "granularity": "instruction"})
            client.wait_for_response("next", timeout_s=10.0)
            found = client.wait_for_all(
                {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=10.0)
            if "stopped" not in found:
                print(f"FAILED: step {i} never stopped", file=sys.stderr)
                return 1
            pc = current_pc(client, tid)
            if pc is None:
                print(f"FAILED: could not read PC after step {i}", file=sys.stderr)
                return 1
            addresses.append(pc)
    finally:
        disconnect(client)
        client.proc.terminate()

    Path(args.output_json).write_text(json.dumps({
        "program": args.program_path,
        "addresses": [f"0x{a:x}" for a in addresses],
    }, indent=2))
    print(f"-> wrote {len(addresses)} addresses to {args.output_json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
