#!/usr/bin/env python3
"""Phase 8: deliberately hangs the board via ES0596 SS2.2.17, probes AP0/TMC survivability. HIGH RISK, LAST."""

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dap_client import launch, spawn, telnet_command  # noqa: E402
from flash_target import flash  # noqa: E402

CHIPNAME = "stm32h7x"
TMC_BASEADDR = 0xE00F4000


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("adapter_path")
    p.add_argument("program_path")
    p.add_argument("tcl_dir")
    p.add_argument("board_cfg")
    p.add_argument("output_dir")
    p.add_argument("--telnet-port", type=int, default=4444)
    p.add_argument("--gdb-port", default="3333")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    main_c_path = str(Path(args.program_path).resolve().parent.parent / "Core" / "Src" / "main.c")
    log_path = out_dir / "engine_stderr.log"

    print("=" * 70)
    print("PHASE 8 - THIS WILL HANG THE BOARD. Confirm known-good firmware is")
    print("backed up and you are ready for a power cycle before proceeding.")
    print("=" * 70)

    print(f"-> flashing {args.program_path}")
    if not flash(args.program_path):
        print("FAILED: flashing did not succeed", file=sys.stderr)
        return 1

    client = spawn(args.adapter_path, stderr_path=str(log_path))
    ok, launch_resp, config_resp = launch(
        client, args.program_path, args.tcl_dir, args.board_cfg, gdb_port=args.gdb_port,
        extra_openocd={"telnetPort": args.telnet_port},
        stop_on_entry=True, timeout_s=30.0)
    if not ok:
        print(f"FAILED: launch did not succeed: launch={launch_resp} configurationDone={config_resp}",
              file=sys.stderr)
        return 1

    client.send("setBreakpoints", {"source": {"path": main_c_path}, "breakpoints": [{"line": 19}]})
    bp_resp = client.wait_for_response("setBreakpoints", timeout_s=10.0)
    if not bp_resp or not bp_resp.get("success"):
        print(f"FAILED: setBreakpoints did not succeed: {bp_resp}", file=sys.stderr)
        return 1
    print(f"-> breakpoint set: {bp_resp.get('body')}")

    client.send("threads")
    resp = client.wait_for_response("threads", timeout_s=10.0)
    threads = (resp.get("body") or {}).get("threads", []) if resp else []
    thread_id = threads[0]["id"] if threads else None

    print("-> pre-hang sanity check: TMC status via telnet/AP0")
    try:
        pre_hang = telnet_command(f"{CHIPNAME}.ap0 mdw {TMC_BASEADDR:#x}", port=args.telnet_port)
    except OSError as e:
        pre_hang = f"<connection failed: {e}>"
    print(f"   {pre_hang!r}")

    print("-> continue to breakpoint (before the GFXMMU access)")
    client.send("continue", {"threadId": thread_id})
    found = client.wait_for_all(
        {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=20.0)
    if "stopped" not in found:
        print("FAILED: never hit the breakpoint before the access", file=sys.stderr)
        return 1

    print("-> trailerTraceEnable (armed while halted, before the fault)")
    client.send("trailerTraceEnable")
    print(f"   {client.wait_for_response('trailerTraceEnable', timeout_s=10.0)}")

    result = {"pre_hang_ap0_probe": pre_hang}

    print("-> stepping over the GFXMMU access now - expect a hang")
    next_seq = client.send("next", {"threadId": thread_id})
    next_resp = client.wait_for_response("next", timeout_s=8.0)
    result["step_response"] = next_resp
    print(f"   step response (None means no answer within timeout): {next_resp}")

    print("-> attempt (a): normal path (AP1/CPU) - readMemory at a known-good address")
    client.send("readMemory", {"memoryReference": "0xE000ED00", "offset": 0, "count": 4})
    ap1_resp = client.wait_for_response("readMemory", timeout_s=8.0)
    result["ap1_readmemory_after_hang"] = ap1_resp
    print(f"   {ap1_resp}")

    print("-> attempt (b): raw AP0 read of TMC status via telnet, independent of the DAP session")
    try:
        post_hang_ap0 = telnet_command(f"{CHIPNAME}.ap0 mdw {TMC_BASEADDR:#x}", port=args.telnet_port)
    except OSError as e:
        post_hang_ap0 = f"<connection failed: {e}>"
    result["post_hang_ap0_probe"] = post_hang_ap0
    print(f"   {post_hang_ap0!r}")

    if "connection failed" not in post_hang_ap0 and post_hang_ap0.strip():
        print("-> AP0 responded post-hang: attempting to drain the TMC via telnet")
        drain_capture_path = out_dir / "post_hang_drain.bin"
        try:
            drain_result = telnet_command(
                f"{CHIPNAME}.etf configure -output {drain_capture_path}; {CHIPNAME}.etf disable",
                port=args.telnet_port)
        except OSError as e:
            drain_result = f"<connection failed: {e}>"
        result["post_hang_drain_command"] = drain_result
        print(f"   {drain_result!r}")
    else:
        print("-> AP0 did not respond either - negative result, recorded as such")

    (out_dir / "result.json").write_text(json.dumps(result, indent=2, default=str))
    print(f"-> wrote {out_dir}/result.json")
    print("=" * 70)
    print("POWER CYCLE THE BOARD NOW to recover, then reflash known-good firmware.")
    print("=" * 70)

    if client.proc.poll() is None:
        client.proc.terminate()
        try:
            client.proc.wait(timeout=5.0)
        except Exception:  # noqa: BLE001
            client.proc.kill()

    return 0


if __name__ == "__main__":
    sys.exit(main())
