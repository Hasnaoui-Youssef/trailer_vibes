#!/usr/bin/env python3
"""Phase 7: run W7 to the fault, pause, capture state-at-halt and the trace leading up to it."""

import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dap_client import CFSR, HFSR, ICSR, disconnect, launch, read_memory_u32, spawn, telnet_command  # noqa: E402
from flash_target import flash  # noqa: E402

CHIPNAME = "stm32h7x"
HARDFAULT_HANDLER_LINE = 84  # "void HardFault_Handler(void)" in stm32h7rsxx_it.c


def find_symbol_address(elf_path: str, symbol: str) -> int:
    out = subprocess.run(["arm-none-eabi-nm", elf_path], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] == symbol:
            return int(parts[0], 16)
    raise RuntimeError(f"symbol '{symbol}' not found in {elf_path}")


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


def main() -> int:
    args = parse_args()
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    record_addr = find_symbol_address(args.program_path, "record")
    safe_action_addr = find_symbol_address(args.program_path, "SafeAction")
    on_complete_addr = record_addr + 8  # buffer[8] precedes on_complete in record_t
    print(f"-> record @ 0x{record_addr:x}, on_complete @ 0x{on_complete_addr:x}, "
          f"SafeAction @ 0x{safe_action_addr:x}")

    print(f"-> flashing {args.program_path}")
    if not flash(args.program_path):
        print("FAILED: flashing did not succeed", file=sys.stderr)
        return 1

    capture_path = out_dir / "capture.bin"
    capture_path.unlink(missing_ok=True)  # the TMC sink opens its output file in append mode
    log_path = out_dir / "engine_stderr.log"
    client = spawn(args.adapter_path, stderr_path=str(log_path))

    ok, launch_resp, config_resp = launch(
        client, args.program_path, args.tcl_dir, args.board_cfg, gdb_port=args.gdb_port,
        extra_openocd={"telnetPort": str(args.telnet_port)}, stop_on_entry=True, timeout_s=30.0)
    if not ok:
        print(f"FAILED: launch did not succeed: launch={launch_resp} configurationDone={config_resp}",
              file=sys.stderr)
        return 1

    print(telnet_command(f"{CHIPNAME}.etf configure -output {capture_path}", port=args.telnet_port))

    it_c = Path(args.program_path).resolve().parent.parent / "Core" / "Src" / "stm32h7rsxx_it.c"
    client.send("setBreakpoints", {"source": {"path": str(it_c)}, "breakpoints": [{"line": HARDFAULT_HANDLER_LINE}]})
    bp_resp = client.wait_for_response("setBreakpoints", timeout_s=10.0)
    bps = (bp_resp.get("body") or {}).get("breakpoints", []) if bp_resp else []
    if not bps or not bps[0].get("verified"):
        print(f"FAILED: breakpoint at {it_c}:{HARDFAULT_HANDLER_LINE} did not verify: {bp_resp}", file=sys.stderr)
        return 1

    client.send("threads")
    resp = client.wait_for_response("threads", timeout_s=10.0)
    threads = (resp.get("body") or {}).get("threads", []) if resp else []
    if not threads:
        print("FAILED: no threads after launch", file=sys.stderr)
        return 1
    thread_id = threads[0]["id"]

    print("-> trailerTraceEnable")
    client.send("trailerTraceEnable")
    print(f"   {client.wait_for_response('trailerTraceEnable', timeout_s=10.0)}")

    print("-> continue (stop at HardFault_Handler entry, before its spin loop can overwrite the buffer)")
    client.send("continue", {"threadId": thread_id})
    client.wait_for_response("continue", timeout_s=10.0)
    found = client.wait_for_all(
        {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=10.0)
    if "stopped" not in found:
        print("FAILED: never hit the HardFault_Handler breakpoint", file=sys.stderr)
        return 1

    trace_events = client.events("trailerTraceData")
    (out_dir / "trace_data.json").write_text(json.dumps(trace_events, indent=2))

    print("-> stackTrace (state-at-halt side of the comparison)")
    client.send("stackTrace", {"threadId": thread_id})
    stack_resp = client.wait_for_response("stackTrace", timeout_s=10.0)
    stack_frames = (stack_resp.get("body") or {}).get("stackFrames", []) if stack_resp else []

    state_at_halt = {
        "stackFrames": stack_frames,
        "CFSR": read_memory_u32(client, CFSR),
        "HFSR": read_memory_u32(client, HFSR),
        "ICSR": read_memory_u32(client, ICSR),
        "corrupted_on_complete_pointer": read_memory_u32(client, on_complete_addr),
        "expected_safe_action_address": safe_action_addr,
    }
    (out_dir / "state_at_halt.json").write_text(json.dumps(state_at_halt, indent=2))
    print(f"-> state at halt: {json.dumps(state_at_halt)}")

    disconnect(client)

    print(f"-> wrote {out_dir}/{{trace_data.json, state_at_halt.json, capture.bin}}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
