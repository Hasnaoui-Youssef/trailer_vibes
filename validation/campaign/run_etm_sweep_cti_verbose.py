#!/usr/bin/env python3
"""Phase 4/9.5 re-verification: same CTI-triggered clean-fill methodology as
run_etm_sweep_cti.py, but with a full, auditable log of every drain/rearm/
continue/ack cycle, the engine's own stderr, and an immediate decode right
after capture - built to chase a reproducibility discrepancy in Finding 06's
timestamp_cyclecount numbers, where the currently-committed capture files no
longer decode to the instruction counts the finding reports."""

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from cti_trigger import ack, arm, cleanup, raw_commands, SYS_CTI, M7_CTI  # noqa: E402
from dap_client import disconnect, ensure_dwt_cycle_counter_enabled, launch, spawn, tid_from_threads  # noqa: E402
from dap_client import telnet_command  # noqa: E402
from flash_target import flash  # noqa: E402

CHIPNAME = "stm32h7x"
TRCCONFIGR_ADDR = 0xE0041010

CONFIGURATIONS = {
    "baseline": "",
    "retstack": "-retstack on",
    "timestamp_cyclecount": "-timestamp on -cyclecount on",
    "branch_broadcast": "-branch-broadcast on",
}

STEADY_STATE_LINE = {
    "w1_baseline": 19, "w2_callchain": 48, "w3_branchdense": 41,
    "w4_isr": 54, "w5_dma": 77, "w6_overflow": 21,
}


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("adapter_path")
    p.add_argument("program_path")
    p.add_argument("tcl_dir")
    p.add_argument("board_cfg")
    p.add_argument("output_dir")
    p.add_argument("replay_path")
    p.add_argument("--configs", nargs="+", default=list(CONFIGURATIONS), choices=list(CONFIGURATIONS))
    p.add_argument("--gdb-port", default="3333")
    p.add_argument("--telnet-port", type=int, default=4444)
    return p.parse_args()


def read_trcconfigr(port: int) -> str:
    return telnet_command(f"{CHIPNAME}.cpu0 mdw {TRCCONFIGR_ADDR:#x}", port=port).strip()


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


def decode(replay_path: str, elf: str, capture: Path) -> dict:
    metrics_path = capture.with_suffix(".metrics.json")
    subprocess.run(
        [replay_path, "--elf", elf, "--capture", str(capture), "--format", "json", "--metrics", str(metrics_path),
         "--dump-elements"],
        check=True, capture_output=True, text=True,
    )
    return json.loads(metrics_path.read_text())


def run_one(args, cfg_name: str, etm_extra: str, out_dir: Path, steady_state_line: int) -> bool:
    out_dir.mkdir(parents=True, exist_ok=True)
    capture_path = out_dir / "capture.bin"
    capture_path.unlink(missing_ok=True)
    audit: list[dict] = []
    t0 = time.monotonic()

    def log(event: str, **fields):
        entry = {"t": round(time.monotonic() - t0, 3), "event": event, **fields}
        audit.append(entry)
        print(f"  [{entry['t']:7.3f}s] {event} {fields}")

    log("flashing", program=args.program_path)
    if not flash(args.program_path):
        log("FAILED", reason="flash did not succeed")
        return False

    client = spawn(args.adapter_path, stderr_path=str(out_dir / "engine_stderr.log"))
    ok, launch_resp, config_resp = launch(
        client, args.program_path, args.tcl_dir, args.board_cfg, gdb_port=args.gdb_port,
        raw_commands=raw_commands(), extra_openocd={"telnetPort": str(args.telnet_port)},
        stop_on_entry=True, timeout_s=30.0)
    log("launch", ok=ok)
    if not ok:
        log("FAILED", reason="launch did not succeed", launch_resp=launch_resp, config_resp=config_resp)
        return False

    main_c = Path(args.program_path).resolve().parent.parent / "Core" / "Src" / "main.c"

    try:
        log("trcconfigr_before", value=read_trcconfigr(args.telnet_port))
        print(telnet_command(f"{CHIPNAME}.etf configure -output {capture_path}", port=args.telnet_port))
        if etm_extra:
            configure_out = telnet_command(f"{CHIPNAME}.etm configure {etm_extra}", port=args.telnet_port)
            log("etm_configure", etm_extra=etm_extra, output=configure_out.strip())
        log("trcconfigr_staged", value=read_trcconfigr(args.telnet_port))

        if not ensure_dwt_cycle_counter_enabled(client):
            log("FAILED", reason="could not enable DWT cycle counter")
            return False

        tid = tid_from_threads(client)
        arm(args.telnet_port)
        log("cti_armed")

        client.send("setBreakpoints", {"source": {"path": str(main_c)}, "breakpoints": [{"line": steady_state_line}]})
        bp_resp = client.wait_for_response("setBreakpoints", timeout_s=10.0)
        bps = (bp_resp.get("body") or {}).get("breakpoints", []) if bp_resp else []
        log("breakpoint_set", line=steady_state_line, verified=bool(bps and bps[0].get("verified")))
        if not bps or not bps[0].get("verified"):
            log("FAILED", reason="breakpoint did not verify", bp_resp=bp_resp)
            return False

        iteration = 0
        while True:
            iteration += 1
            drain_and_rearm(client)
            stopped = continue_and_wait(client, tid)
            reason = (stopped or {}).get("body", {}).get("reason")
            size_now = capture_path.stat().st_size if capture_path.exists() else 0
            log("discard_loop_iteration", iteration=iteration, stopped_reason=reason, capture_bytes_so_far=size_now,
                trcconfigr=read_trcconfigr(args.telnet_port))
            if stopped is None:
                log("FAILED", reason="no halt within 10s reaching steady state")
                return False
            if reason == "breakpoint":
                break
            client.send("trailerTraceDisable")
            client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
            ack(SYS_CTI, args.telnet_port)
            ack(M7_CTI, args.telnet_port)

        client.send("setBreakpoints", {"source": {"path": str(main_c)}, "breakpoints": []})
        client.wait_for_response("setBreakpoints", timeout_s=10.0)
        client.send("trailerTraceDisable")
        client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
        size_before_discard = capture_path.stat().st_size if capture_path.exists() else 0
        log("reached_steady_state", discarded_bytes=size_before_discard)
        capture_path.unlink(missing_ok=True)

        drain_and_rearm(client)
        log("final_measurement_armed", trcconfigr=read_trcconfigr(args.telnet_port))
        stopped = continue_and_wait(client, tid)
        reason = (stopped or {}).get("body", {}).get("reason")
        log("final_measurement_halted", stopped_reason=reason,
            capture_bytes=capture_path.stat().st_size if capture_path.exists() else 0)
        if stopped is None:
            log("FAILED", reason="no self-triggered halt within 10s")
            return False

        client.send("trailerTraceDisable")
        client.wait_for_response("trailerTraceDisable", timeout_s=10.0)
        log("trcconfigr_final", value=read_trcconfigr(args.telnet_port))
    finally:
        cleanup(args.telnet_port)
        disconnect(client)
        client.proc.terminate()

    size = capture_path.stat().st_size if capture_path.exists() else 0
    log("capture_written", path=str(capture_path), bytes=size)

    metrics = decode(args.replay_path, args.program_path, capture_path)
    log("decoded", instructionsReconstructed=metrics["instructionsReconstructed"],
        traceRecords=metrics["traceRecords"], gaps=metrics["gaps"],
        gapsByReason=metrics.get("gapsByReason", {}), elementsByKind=metrics.get("elementsByKind", {}))

    (out_dir / "audit.json").write_text(json.dumps({"config": cfg_name, "etm_extra": etm_extra, "log": audit,
                                                       "metrics": metrics}, indent=2))
    print(f"-> {cfg_name}: wrote {capture_path} ({size} bytes), "
          f"{metrics['instructionsReconstructed']} instructions reconstructed")
    return True


def main() -> int:
    args = parse_args()
    base_dir = Path(args.output_dir)
    workload = Path(args.program_path).resolve().parent.parent.name
    steady_state_line = STEADY_STATE_LINE[workload]
    ok_all = True
    for name in args.configs:
        flags = CONFIGURATIONS[name]
        print(f"=== {name} ({flags or 'no extra flags'}) ===")
        ok_all = run_one(args, name, flags, base_dir / name, steady_state_line) and ok_all
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
