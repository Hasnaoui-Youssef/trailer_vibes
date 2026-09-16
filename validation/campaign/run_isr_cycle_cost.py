#!/usr/bin/env python3
"""Phase 9.5: captures one TIM6 ISR entry-to-return window with cycle
counting and timestamping enabled. Bounded by breakpoints at ISR entry and
exit rather than a CTI buffer-full trigger, so the capture holds exactly
one ISR invocation regardless of timer period or buffer capacity.
See validation/findings/19-*.md."""

import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cti_trigger import raw_commands  # noqa: E402
from dap_client import DWT_CYCCNT, disconnect, ensure_dwt_cycle_counter_enabled  # noqa: E402
from dap_client import launch, read_memory_u32, spawn, tid_from_threads, telnet_command  # noqa: E402
from flash_target import flash  # noqa: E402

CHIPNAME = "stm32h7x"
ISR_ENTRY_LINE = 206  # stm32h7rsxx_it.c: TIM6_IRQHandler's first statement
ISR_EXIT_LINE = 207   # stm32h7rsxx_it.c: TIM6_IRQHandler's closing brace


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("adapter_path")
    p.add_argument("program_path")
    p.add_argument("tcl_dir")
    p.add_argument("board_cfg")
    p.add_argument("output_dir")
    p.add_argument("replay_path")
    p.add_argument("--gdb-port", default="3333")
    p.add_argument("--telnet-port", type=int, default=4444)
    return p.parse_args()


def continue_and_wait(client, tid: int, timeout_s: float = 15.0) -> dict | None:
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


def decode(replay_path: str, elf: str, capture: Path, etm_regs: Path) -> dict:
    metrics_path = capture.with_suffix(".metrics.json")
    subprocess.run(
        [replay_path, "--elf", elf, "--capture", str(capture), "--format", "json", "--metrics", str(metrics_path),
         "--etm-regs", str(etm_regs), "--dump-elements"],
        check=True, capture_output=True, text=True,
    )
    return json.loads(metrics_path.read_text())


def set_breakpoint(client, source_path: Path, line: int) -> bool:
    client.send("setBreakpoints", {"source": {"path": str(source_path)}, "breakpoints": [{"line": line}]})
    resp = client.wait_for_response("setBreakpoints", timeout_s=10.0)
    bps = (resp.get("body") or {}).get("breakpoints", []) if resp else []
    return bool(bps and bps[0].get("verified"))


def main() -> int:
    args = parse_args()
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    capture_path = out_dir / "capture.bin"
    capture_path.unlink(missing_ok=True)

    print(f"-> flashing {args.program_path}")
    if not flash(args.program_path):
        print("FAILED: flashing did not succeed", file=sys.stderr)
        return 1

    client = spawn(args.adapter_path, stderr_path=str(out_dir / "engine_stderr.log"))
    ok, launch_resp, config_resp = launch(
        client, args.program_path, args.tcl_dir, args.board_cfg, gdb_port=args.gdb_port,
        raw_commands=raw_commands(), extra_openocd={"telnetPort": str(args.telnet_port)},
        stop_on_entry=True, timeout_s=30.0)
    if not ok:
        print(f"FAILED: launch did not succeed: launch={launch_resp} configurationDone={config_resp}",
              file=sys.stderr)
        return 1

    isr_file = Path(args.program_path).resolve().parent.parent / "Core" / "Src" / "stm32h7rsxx_it.c"

    try:
        print(telnet_command(f"{CHIPNAME}.etf configure -output {capture_path}", port=args.telnet_port))
        print(telnet_command(f"{CHIPNAME}.etm configure -timestamp on -cyclecount on", port=args.telnet_port))

        if not ensure_dwt_cycle_counter_enabled(client):
            print("FAILED: could not enable DWT cycle counter", file=sys.stderr)
            return 1

        tid = tid_from_threads(client)

        if not set_breakpoint(client, isr_file, ISR_ENTRY_LINE):
            print(f"FAILED: breakpoint at {isr_file}:{ISR_ENTRY_LINE} did not verify", file=sys.stderr)
            return 1

        stopped = continue_and_wait(client, tid)
        if stopped is None or stopped.get("body", {}).get("reason") != "breakpoint":
            print(f"FAILED: did not stop at ISR entry: {stopped}", file=sys.stderr)
            return 1
        cyccnt_entry = read_memory_u32(client, DWT_CYCCNT)

        if not set_breakpoint(client, isr_file, ISR_EXIT_LINE):
            print(f"FAILED: breakpoint at {isr_file}:{ISR_EXIT_LINE} did not verify", file=sys.stderr)
            return 1

        drain_and_rearm(client)  # discard everything before ISR entry; the capture starts fresh right here
        stopped = continue_and_wait(client, tid)
        if stopped is None or stopped.get("body", {}).get("reason") != "breakpoint":
            print(f"FAILED: did not stop at ISR exit: {stopped}", file=sys.stderr)
            return 1
        cyccnt_exit = read_memory_u32(client, DWT_CYCCNT)

        client.send("trailerTraceDisable")
        client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
    finally:
        disconnect(client)
        client.proc.terminate()

    size = capture_path.stat().st_size if capture_path.exists() else 0
    print(f"-> wrote {capture_path} ({size} bytes)")

    etm_regs = Path(__file__).resolve().parent.parent / "analysis" / "etm_regs_timestamp_cyclecount.json"
    metrics = decode(args.replay_path, args.program_path, capture_path, etm_regs)
    dwt_delta = None
    if cyccnt_entry is not None and cyccnt_exit is not None:
        dwt_delta = (cyccnt_exit - cyccnt_entry) & 0xFFFFFFFF
    print(json.dumps({"instructionsReconstructed": metrics["instructionsReconstructed"],
                       "elementsByKind": metrics.get("elementsByKind", {}),
                       "gapsByReason": metrics.get("gapsByReason", {}),
                       "dwtCyccntEntry": cyccnt_entry, "dwtCyccntExit": cyccnt_exit,
                       "dwtCycleDelta": dwt_delta}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
