#!/usr/bin/env python3
"""Phase 3: flash, launch, arm trace, run for a fixed duration, pause once, save the capture."""

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dap_client import disconnect, launch, spawn, telnet_command  # noqa: E402
from flash_target import flash  # noqa: E402

CHIPNAME = "stm32h7x"


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("adapter_path")
    p.add_argument("program_path")
    p.add_argument("tcl_dir")
    p.add_argument("board_cfg")
    p.add_argument("output_dir")
    p.add_argument("--duration-s", type=float, required=True)
    p.add_argument("--etm-extra", default="", help="extra 'stm32h7x.etm configure' flags, e.g. '-timestamp on -cyclecount on'")
    p.add_argument("--gdb-port", default="3333")
    p.add_argument("--telnet-port", type=int, default=4444)
    p.add_argument("--repeat", type=int, default=1)
    return p.parse_args()


def run_one(args, run_dir: Path) -> bool:
    run_dir.mkdir(parents=True, exist_ok=True)
    capture_path = run_dir / "capture.bin"
    capture_path.unlink(missing_ok=True)  # the TMC sink opens its output file in append mode
    log_path = run_dir / "engine_stderr.log"

    print(f"-> flashing {args.program_path}")
    if not flash(args.program_path):
        print("FAILED: flashing did not succeed", file=sys.stderr)
        return False

    client = spawn(args.adapter_path, stderr_path=str(log_path))
    ok, launch_resp, config_resp = launch(
        client, args.program_path, args.tcl_dir, args.board_cfg, gdb_port=args.gdb_port,
        extra_openocd={"telnetPort": str(args.telnet_port)}, stop_on_entry=True, timeout_s=30.0)
    if not ok:
        print(f"FAILED: launch did not succeed: launch={launch_resp} configurationDone={config_resp}",
              file=sys.stderr)
        return False

    print(telnet_command(f"{CHIPNAME}.etf configure -output {capture_path}", port=args.telnet_port))
    if args.etm_extra.strip():
        print(telnet_command(f"{CHIPNAME}.etm configure {args.etm_extra.strip()}", port=args.telnet_port))

    client.send("threads")
    resp = client.wait_for_response("threads", timeout_s=10.0)
    threads = (resp.get("body") or {}).get("threads", []) if resp else []
    if not threads:
        print("FAILED: no threads after launch", file=sys.stderr)
        return False
    thread_id = threads[0]["id"]

    print("-> trailerTraceEnable")
    client.send("trailerTraceEnable")
    enable_resp = client.wait_for_response("trailerTraceEnable", timeout_s=10.0)
    print(f"   {enable_resp}")

    print(f"-> continue, run for {args.duration_s:.1f}s")
    client.send("continue", {"threadId": thread_id})
    client.wait_for_response("continue", timeout_s=10.0)
    time.sleep(args.duration_s)

    print("-> pause")
    client.send("pause", {"threadId": thread_id})
    found = client.wait_for_all(
        {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=20.0)
    if "stopped" not in found:
        print("FAILED: never saw 'stopped' after pause", file=sys.stderr)
        return False

    trace_events = client.events("trailerTraceData")
    (run_dir / "trace_data.json").write_text(json.dumps(trace_events, indent=2))

    client.send("trailerTraceStatus")
    status_resp = client.wait_for_response("trailerTraceStatus", timeout_s=10.0)
    (run_dir / "trace_status.json").write_text(json.dumps((status_resp or {}).get("body", {}), indent=2))

    client.send("trailerTraceDisable")
    client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
    disconnect(client)

    meta = {
        "program": str(args.program_path),
        "duration_s": args.duration_s,
        "etm_extra": args.etm_extra,
        "trace_events_count": len(trace_events),
        "capture_bytes": capture_path.stat().st_size if capture_path.exists() else 0,
    }
    (run_dir / "meta.json").write_text(json.dumps(meta, indent=2))
    print(f"-> {run_dir}: {meta}")
    return True


def main() -> int:
    args = parse_args()
    base_dir = Path(args.output_dir)
    ok_all = True
    for rep in range(args.repeat):
        run_dir = base_dir if args.repeat == 1 else base_dir / f"rep{rep}"
        ok_all = run_one(args, run_dir) and ok_all
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
