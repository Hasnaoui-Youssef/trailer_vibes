#!/usr/bin/env python3
"""Phase 4: one CTI-triggered clean fill per ETM configuration, from steady state."""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cti_trigger import ack, arm, cleanup, raw_commands, SYS_CTI, M7_CTI  # noqa: E402
from dap_client import disconnect, ensure_dwt_cycle_counter_enabled, launch, spawn, tid_from_threads  # noqa: E402
from dap_client import telnet_command  # noqa: E402
from flash_target import flash  # noqa: E402

CHIPNAME = "stm32h7x"

# -data-addr/-data-value deliberately excluded: OpenCSD hard-rejects TRCCONFIGR.INSTP0.
CONFIGURATIONS = {
    "baseline": "",
    "retstack": "-retstack on",
    "timestamp_cyclecount": "-timestamp on -cyclecount on",
    "branch_broadcast": "-branch-broadcast on",
}

# The first statement of the workload's own repeating body - past all boot code.
STEADY_STATE_LINE = {
    "w1_baseline": 19,
    "w2_callchain": 48,
    "w3_branchdense": 41,
    "w4_isr": 54,
    "w5_dma": 77,
    "w6_overflow": 21,
}


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("adapter_path")
    p.add_argument("program_path")
    p.add_argument("tcl_dir")
    p.add_argument("board_cfg")
    p.add_argument("output_dir")
    p.add_argument("--gdb-port", default="3333")
    p.add_argument("--telnet-port", type=int, default=4444)
    return p.parse_args()


def continue_and_wait(client, tid: int, timeout_s: float = 10.0) -> dict | None:
    client.send("continue", {"threadId": tid})
    client.wait_for_response("continue", timeout_s=timeout_s)
    found = client.wait_for_all(
        {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=timeout_s)
    return found.get("stopped", (None, None))[1]


def drain_and_rearm(client) -> None:
    client.send("trailerTraceDisable")
    client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
    client.send("trailerTraceEnable")
    client.wait_for_response("trailerTraceEnable", timeout_s=10.0)


def run_one(args, cfg_name: str, etm_extra: str, out_dir: Path, steady_state_line: int) -> bool:
    out_dir.mkdir(parents=True, exist_ok=True)
    capture_path = out_dir / "capture.bin"
    capture_path.unlink(missing_ok=True)  # the TMC sink opens its output file in append mode

    print(f"-> flashing {args.program_path}")
    if not flash(args.program_path):
        print("FAILED: flashing did not succeed", file=sys.stderr)
        return False

    client = spawn(args.adapter_path)
    ok, launch_resp, config_resp = launch(
        client, args.program_path, args.tcl_dir, args.board_cfg, gdb_port=args.gdb_port,
        raw_commands=raw_commands(), extra_openocd={"telnetPort": str(args.telnet_port)},
        stop_on_entry=True, timeout_s=30.0)
    if not ok:
        print(f"FAILED: launch did not succeed: launch={launch_resp} configurationDone={config_resp}",
              file=sys.stderr)
        return False

    main_c = Path(args.program_path).resolve().parent.parent / "Core" / "Src" / "main.c"

    try:
        print(telnet_command(f"{CHIPNAME}.etf configure -output {capture_path}", port=args.telnet_port))
        if etm_extra:
            print(telnet_command(f"{CHIPNAME}.etm configure {etm_extra}", port=args.telnet_port))

        if not ensure_dwt_cycle_counter_enabled(client):
            print(f"FAILED [{cfg_name}]: could not enable DWT cycle counter", file=sys.stderr)
            return False

        tid = tid_from_threads(client)
        arm(args.telnet_port)

        client.send("setBreakpoints", {"source": {"path": str(main_c)}, "breakpoints": [{"line": steady_state_line}]})
        bp_resp = client.wait_for_response("setBreakpoints", timeout_s=10.0)
        bps = (bp_resp.get("body") or {}).get("breakpoints", []) if bp_resp else []
        if not bps or not bps[0].get("verified"):
            print(f"FAILED [{cfg_name}]: breakpoint at {main_c}:{steady_state_line} did not verify: {bp_resp}",
                  file=sys.stderr)
            return False

        while True:
            drain_and_rearm(client)
            stopped = continue_and_wait(client, tid)
            if stopped is None:
                print(f"FAILED [{cfg_name}]: no halt within 10s reaching steady state", file=sys.stderr)
                return False
            if stopped.get("body", {}).get("reason") == "breakpoint":
                break
            client.send("trailerTraceDisable")
            client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
            ack(SYS_CTI, args.telnet_port)
            ack(M7_CTI, args.telnet_port)

        client.send("setBreakpoints", {"source": {"path": str(main_c)}, "breakpoints": []})
        client.wait_for_response("setBreakpoints", timeout_s=10.0)
        client.send("trailerTraceDisable")  # discard whatever filled since the last re-arm, up to the breakpoint
        client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
        capture_path.unlink(missing_ok=True)

        drain_and_rearm(client)
        stopped = continue_and_wait(client, tid)
        if stopped is None:
            print(f"FAILED [{cfg_name}]: no self-triggered halt within 10s", file=sys.stderr)
            return False

        client.send("trailerTraceDisable")
        client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
    finally:
        cleanup(args.telnet_port)
        disconnect(client)
        client.proc.terminate()

    size = capture_path.stat().st_size if capture_path.exists() else 0
    print(f"-> {cfg_name}: wrote {capture_path} ({size} bytes)")
    return True


def main() -> int:
    args = parse_args()
    base_dir = Path(args.output_dir)
    workload = Path(args.program_path).resolve().parent.parent.name
    steady_state_line = STEADY_STATE_LINE[workload]
    ok_all = True
    for name, flags in CONFIGURATIONS.items():
        print(f"=== {name} ({flags or 'no extra flags'}) ===")
        ok_all = run_one(args, name, flags, base_dir / name, steady_state_line) and ok_all
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
