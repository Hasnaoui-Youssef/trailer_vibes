#!/usr/bin/env python3
"""Reads ETMv4 TRCCONFIGR before staging, after staging, and after a real resume."""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cti_trigger import arm, cleanup, raw_commands  # noqa: E402
from dap_client import disconnect, ensure_dwt_cycle_counter_enabled, launch, spawn, tid_from_threads  # noqa: E402
from dap_client import telnet_command  # noqa: E402
from flash_target import flash  # noqa: E402

CHIPNAME = "stm32h7x"
TRCCONFIGR_ADDR = 0xE0041010


def read_trcconfigr(port: int) -> str:
    return telnet_command(f"{CHIPNAME}.cpu0 mdw {TRCCONFIGR_ADDR:#x}", port=port).strip()


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("adapter_path")
    p.add_argument("program_path")
    p.add_argument("tcl_dir")
    p.add_argument("board_cfg")
    p.add_argument("--etm-extra", default="-retstack on")
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
        raw_commands=raw_commands(), extra_openocd={"telnetPort": str(args.telnet_port)},
        stop_on_entry=True, timeout_s=30.0)
    if not ok:
        print(f"FAILED: launch did not succeed: launch={launch_resp} configurationDone={config_resp}",
              file=sys.stderr)
        return 1

    try:
        print(f"TRCCONFIGR before configure: {read_trcconfigr(args.telnet_port)}")
        print(telnet_command(f"{CHIPNAME}.etm configure {args.etm_extra}", port=args.telnet_port))
        print(f"TRCCONFIGR after configure, before resume: {read_trcconfigr(args.telnet_port)}")

        if not ensure_dwt_cycle_counter_enabled(client):
            print("FAILED: could not enable DWT cycle counter", file=sys.stderr)
            return 1

        tid = tid_from_threads(client)
        arm(args.telnet_port)

        client.send("trailerTraceDisable")
        client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
        client.send("trailerTraceEnable")
        client.wait_for_response("trailerTraceEnable", timeout_s=10.0)

        client.send("continue", {"threadId": tid})
        client.wait_for_response("continue", timeout_s=10.0)
        found = client.wait_for_all(
            {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=10.0)
        if "stopped" not in found:
            print("FAILED: no self-triggered halt within 10s", file=sys.stderr)
            return 1

        print(f"TRCCONFIGR after resume+halt: {read_trcconfigr(args.telnet_port)}")
    finally:
        cleanup(args.telnet_port)
        disconnect(client)
        client.proc.terminate()

    return 0


if __name__ == "__main__":
    sys.exit(main())
