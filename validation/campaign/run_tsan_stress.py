#!/usr/bin/env python3
"""Phase 6: trace + watches + variables/stackTrace/evaluate/cancel hammering, under a linux-tsan trailer-dap build."""

import argparse
import json
import re
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dap_client import Client, disconnect, launch, spawn, telnet_command  # noqa: E402
from flash_target import flash  # noqa: E402

CHIPNAME = "stm32h7x"


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("adapter_path")
    p.add_argument("program_path")
    p.add_argument("tcl_dir")
    p.add_argument("board_cfg")
    p.add_argument("--duration-s", type=float, default=300.0)
    p.add_argument("--inspection-interval-s", type=float, default=0.02)
    p.add_argument("--gdb-port", default="3333")
    p.add_argument("--telnet-port", type=int, default=4444)
    p.add_argument("--stderr-log", default="tsan_stress_stderr.log")
    return p.parse_args()


def hammer_inspection(client: Client, thread_id_box: list, stop_event: threading.Event, counters: dict,
                       interval_s: float = 0.02):
    """Fires stackTrace/evaluate/threads at a fixed rate, racing run/stop."""
    seq = 0
    while not stop_event.is_set():
        tid = thread_id_box[0]
        if tid is None:
            time.sleep(0.01)
            continue
        seq += 1
        client.send("stackTrace", {"threadId": tid})
        client.send("evaluate", {"expression": "1+1", "context": "repl"})
        counters["inspection_requests"] = counters.get("inspection_requests", 0) + 2
        if seq % 7 == 0:
            client.send("threads")
            counters["inspection_requests"] += 1
        time.sleep(interval_s)


def cycle_run_control(client: Client, thread_id_box: list, stop_event: threading.Event,
                       counters: dict, cancel_every: int = 5):
    """Cycles continue/pause, cancelling one continue in cancel_every passes."""
    pass_count = 0
    while not stop_event.is_set():
        tid = thread_id_box[0]
        if tid is None:
            time.sleep(0.05)
            continue
        pass_count += 1
        cont_seq = client.send("continue", {"threadId": tid})
        counters["continues"] = counters.get("continues", 0) + 1
        if pass_count % cancel_every == 0:
            time.sleep(0.02)
            client.send("cancel", {"requestId": cont_seq})
            counters["cancels"] = counters.get("cancels", 0) + 1
        time.sleep(0.15)
        client.send("pause", {"threadId": tid})
        counters["pauses"] = counters.get("pauses", 0) + 1
        time.sleep(0.05)


def scan_for_tsan_reports(stderr_path: Path) -> list:
    if not stderr_path.exists():
        return []
    text = stderr_path.read_text(errors="replace")
    return re.findall(r"WARNING: ThreadSanitizer:.*?(?=\n==\d+==|$)", text, re.DOTALL)


def main() -> int:
    args = parse_args()

    print(f"-> flashing {args.program_path}")
    if not flash(args.program_path):
        print("FAILED: flashing did not succeed", file=sys.stderr)
        return 1

    client = spawn(args.adapter_path, stderr_path=args.stderr_log)

    ok, launch_resp, config_resp = launch(
        client, args.program_path, args.tcl_dir, args.board_cfg, gdb_port=args.gdb_port,
        extra_openocd={"telnetPort": str(args.telnet_port)},
        stop_on_entry=True, timeout_s=30.0)
    if not ok:
        print(f"FAILED: launch/configurationDone did not succeed: "
              f"launch={launch_resp} configurationDone={config_resp}", file=sys.stderr)
        return 1

    print(telnet_command(f"{CHIPNAME}.etf configure -mode circular", port=args.telnet_port))
    print(telnet_command(f"{CHIPNAME}.etm configure -retstack on", port=args.telnet_port))

    client.send("threads")
    resp = client.wait_for_response("threads", timeout_s=10.0)
    threads = (resp.get("body") or {}).get("threads", []) if resp else []
    if not threads:
        print("FAILED: no threads after launch", file=sys.stderr)
        return 1
    thread_id_box = [threads[0]["id"]]

    print("-> trailerTraceEnable")
    client.send("trailerTraceEnable")
    trace_resp = client.wait_for_response("trailerTraceEnable", timeout_s=10.0)
    print(f"   {trace_resp}")

    print("-> trailerWatchStart")
    client.send("trailerWatchStart", {"address": "0x24000000", "size": 64, "intervalMs": 50})
    watch_resp = client.wait_for_response("trailerWatchStart", timeout_s=10.0)
    print(f"   {watch_resp}")

    counters: dict = {}
    stop_event = threading.Event()
    threads_list = [
        threading.Thread(target=hammer_inspection,
                          args=(client, thread_id_box, stop_event, counters, args.inspection_interval_s)),
        threading.Thread(target=cycle_run_control, args=(client, thread_id_box, stop_event, counters)),
    ]
    for t in threads_list:
        t.daemon = True
        t.start()

    print(f"-> stress running for {args.duration_s:.0f}s "
          f"(continue/pause/cancel cycling + concurrent variables/stackTrace/evaluate + trace + memory watch)")
    start = time.monotonic()
    while time.monotonic() - start < args.duration_s:
        time.sleep(1.0)
        client.send("trailerTraceStatus")

    stop_event.set()
    for t in threads_list:
        t.join(timeout=5.0)

    client.send("trailerWatchStop")
    client.wait_for_response("trailerWatchStop", timeout_s=10.0)
    disconnect(client)

    print(f"-> counters: {json.dumps(counters)}")

    time.sleep(1.0)
    if client.proc.poll() is None:
        client.proc.terminate()
        try:
            client.proc.wait(timeout=5.0)
        except Exception:  # noqa: BLE001
            client.proc.kill()

    reports = scan_for_tsan_reports(Path(args.stderr_log))
    print(f"-> ThreadSanitizer reports found: {len(reports)}")
    for i, report in enumerate(reports):
        print(f"--- report {i + 1} ---")
        print(report)

    return 1 if reports else 0


if __name__ == "__main__":
    sys.exit(main())
