#!/usr/bin/env python3
"""Live hardware smoke check for stepping (next/stepIn/stepOut) against the
stm32h7s3x_dummy board's dbg_h7rs_Boot.elf: stepping over/into/out of a call
must always halt, and hardware-comparator exhaustion must never hang.

Unlike launch_smoke_test.py's Client, every wait here has a real wall-clock
timeout via a background reader thread, not just a message-count cap - a
client that can itself block forever on a blocking read would defeat the
point of a test whose job is to catch a real hang.

Usage: stepping_smoke_test.py <path-to-trailer-dap> <path-to-firmware.elf>
           <path-to-openocd-tcl-dir> <path-to-board-cfg> [gdb-port]
"""

import json
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

from launch_smoke_test import frame, read_message


class Client:
    def __init__(self, proc):
        self.proc = proc
        self.seq = 0
        self.q: "queue.Queue[dict | Exception]" = queue.Queue()
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while True:
            try:
                msg = read_message(self.proc.stdout)
            except Exception as e:  # noqa: BLE001
                self.q.put(e)
                return
            self.q.put(msg)

    def send(self, command: str, arguments: dict | None = None) -> int:
        self.seq += 1
        msg = {"type": "request", "seq": self.seq, "command": command}
        if arguments is not None:
            msg["arguments"] = arguments
        self.proc.stdin.write(frame(msg))
        self.proc.stdin.flush()
        return self.seq

    def _read(self, timeout_s: float) -> dict | None:
        try:
            msg = self.q.get(timeout=timeout_s)
        except queue.Empty:
            return None
        if isinstance(msg, Exception):
            return None
        print(f"  <- {msg.get('type')} {msg.get('event') or msg.get('command')}: "
              f"{json.dumps(msg.get('body'))[:200]}")
        return msg

    def wait_for_response(self, command: str, timeout_s: float = 15.0) -> dict | None:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            msg = self._read(deadline - time.monotonic())
            if msg is None:
                return None
            if msg.get("type") == "response" and msg.get("command") == command:
                return msg
        return None

    def wait_for_all(self, predicates: dict, timeout_s: float = 15.0) -> dict:
        found = {}
        deadline = time.monotonic() + timeout_s
        i = 0
        while time.monotonic() < deadline and len(found) < len(predicates):
            msg = self._read(deadline - time.monotonic())
            if msg is None:
                break
            for name, predicate in predicates.items():
                if name not in found and predicate(msg):
                    found[name] = (i, msg)
            i += 1
        return found


def restart_and_wait(client: Client, label: str) -> bool:
    """Forces a real reset so every case starts from a known PC - `launch`
    alone does not reset the core, only `restart` does."""
    print(f"-> restart ({label})")
    client.send("restart")
    if client.wait_for_response("restart") is None:
        print(f"FAILED [{label}]: restart never responded", file=sys.stderr)
        return False
    if client.wait_for_all({"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}).get(
            "stopped") is None:
        print(f"FAILED [{label}]: never saw 'stopped' after restart", file=sys.stderr)
        return False
    return True


def set_breakpoints(client: Client, path: Path, lines: list[int], label: str) -> list[dict] | None:
    print(f"-> setBreakpoints ({path}:{lines})")
    client.send("setBreakpoints", {"source": {"path": str(path)}, "breakpoints": [{"line": ln} for ln in lines]})
    resp = client.wait_for_response("setBreakpoints")
    if resp is None or not resp.get("success"):
        print(f"FAILED [{label}]: setBreakpoints never succeeded: {resp}", file=sys.stderr)
        return None
    return (resp.get("body") or {}).get("breakpoints", [])


def continue_to_stop(client: Client, thread_id: int, label: str, timeout_s: float = 20.0) -> dict | None:
    client.send("continue", {"threadId": thread_id})
    found = client.wait_for_all(
        {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=timeout_s)
    if "stopped" not in found:
        print(f"FAILED [{label}]: never hit a breakpoint within {timeout_s}s", file=sys.stderr)
        return None
    return found["stopped"][1]


def case_a_step_over_hal_init(client: Client, thread_id: int, main_c: Path) -> bool:
    label = "case a: next over HAL_Init()"
    if not restart_and_wait(client, label):
        return False
    if set_breakpoints(client, main_c, [81], label) is None:
        return False
    stopped = continue_to_stop(client, thread_id, label)
    if stopped is None:
        return False
    pc_before = (stopped.get("body") or {}).get("hitBreakpointIds")

    print("-> next (step over HAL_Init() call)")
    client.send("next", {"threadId": thread_id})
    found = client.wait_for_all(
        {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=10.0)
    if "stopped" not in found:
        print(f"FAILED [{label}]: next() did not halt within 10s - HANG", file=sys.stderr)
        return False
    body = found["stopped"][1].get("body") or {}
    if body.get("reason") != "step":
        print(f"FAILED [{label}]: expected reason 'step', got {body}", file=sys.stderr)
        return False
    print(f"   OK: {label} (pc_before hit ids={pc_before})")
    return True


def case_b_step_in_hal_init(client: Client, thread_id: int, main_c: Path) -> bool:
    label = "case b: stepIn into HAL_Init()"
    if not restart_and_wait(client, label):
        return False
    if set_breakpoints(client, main_c, [81], label) is None:
        return False
    if continue_to_stop(client, thread_id, label) is None:
        return False

    print("-> stepIn")
    client.send("stepIn", {"threadId": thread_id})
    found = client.wait_for_all(
        {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=10.0)
    if "stopped" not in found:
        print(f"FAILED [{label}]: stepIn did not halt within 10s - HANG", file=sys.stderr)
        return False

    print("-> stackTrace")
    client.send("stackTrace", {"threadId": thread_id})
    resp = client.wait_for_response("stackTrace")
    frames = (resp.get("body") or {}).get("stackFrames", []) if resp else []
    top_name = frames[0].get("name") if frames else None
    if top_name != "HAL_Init":
        print(f"FAILED [{label}]: expected top frame 'HAL_Init', got {top_name} ({frames})", file=sys.stderr)
        return False
    print(f"   OK: {label} (top frame: {top_name})")
    return True


def case_c_step_out_of_hal_init(client: Client, thread_id: int, main_c: Path) -> bool:
    label = "case c: stepOut of HAL_Init()"
    if not restart_and_wait(client, label):
        return False
    if set_breakpoints(client, main_c, [81], label) is None:
        return False
    if continue_to_stop(client, thread_id, label) is None:
        return False

    client.send("stepIn", {"threadId": thread_id})
    if "stopped" not in client.wait_for_all(
            {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=10.0):
        print(f"FAILED [{label}]: setup stepIn did not halt within 10s", file=sys.stderr)
        return False

    print("-> stepOut")
    client.send("stepOut", {"threadId": thread_id})
    found = client.wait_for_all(
        {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=10.0)
    if "stopped" not in found:
        print(f"FAILED [{label}]: stepOut did not halt within 10s - HANG", file=sys.stderr)
        return False

    client.send("stackTrace", {"threadId": thread_id})
    resp = client.wait_for_response("stackTrace")
    frames = (resp.get("body") or {}).get("stackFrames", []) if resp else []
    top_name = frames[0].get("name") if frames else None
    if top_name != "main":
        print(f"FAILED [{label}]: expected top frame 'main' after stepping out, got {top_name}", file=sys.stderr)
        return False
    print(f"   OK: {label} (top frame: {top_name})")
    return True


def case_d_step_through_reset_handler(client: Client, thread_id: int) -> bool:
    """Reset_Handler's line table is a fixed oracle - stepping through it must
    hit exactly these instruction addresses, in order."""
    label = "case d: next x5 through Reset_Handler"
    if not restart_and_wait(client, label):
        return False

    # Read after each `next`, so the first entry is one step past 0x08001214.
    expected_pcs = ["0x8001216", "0x8001218", "0x800121C", "0x800121E", "0x8001220"]
    seen_pcs = []
    for i in range(5):
        client.send("next", {"threadId": thread_id})
        found = client.wait_for_all(
            {"stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped"}, timeout_s=10.0)
        if "stopped" not in found:
            print(f"FAILED [{label}]: next() #{i + 1} did not halt within 10s - HANG", file=sys.stderr)
            return False
        client.send("stackTrace", {"threadId": thread_id})
        resp = client.wait_for_response("stackTrace")
        frames = (resp.get("body") or {}).get("stackFrames", []) if resp else []
        pc = frames[0].get("instructionPointerReference") if frames else None
        seen_pcs.append(pc)

    if seen_pcs != expected_pcs:
        print(f"FAILED [{label}]: expected PC sequence {expected_pcs}, got {seen_pcs}", file=sys.stderr)
        return False
    print(f"   OK: {label} (PCs: {seen_pcs})")
    return True


def case_e_comparator_exhaustion_never_hangs(client: Client, thread_id: int, main_c: Path) -> bool:
    """8 user breakpoints occupy this Cortex-M7's full FPB comparator budget,
    leaving `next` nothing free for its own internal bookkeeping breakpoint.
    `next` must always produce some response/event - never hang - regardless
    of whether LLDB reports a clean failure or lets the core free-run into a
    different already-installed breakpoint."""
    label = "case e: comparator exhaustion never hangs"
    if not restart_and_wait(client, label):
        return False
    # Excludes line 88 (HAL_Init()'s own return address) so the internal
    # step-over breakpoint can't reuse an already-installed comparator there.
    lines = [73, 76, 81, 104, 105, 106, 107, 108]
    bps = set_breakpoints(client, main_c, lines, label)
    if bps is None:
        return False
    if len(bps) != 8 or not all(bp.get("verified") for bp in bps):
        print(f"FAILED [{label}]: expected 8 verified breakpoints, got {bps}", file=sys.stderr)
        return False
    # 73/76 have no calls to step over; land on 81 (HAL_Init()) with all 8
    # comparators already occupied by real breakpoints.
    for _ in range(2):
        if continue_to_stop(client, thread_id, label) is None:
            return False
    if continue_to_stop(client, thread_id, label) is None:
        return False

    print("-> next (no free hardware comparators left for the internal step-over breakpoint)")
    client.send("next", {"threadId": thread_id})
    found = client.wait_for_all(
        {
            "response": lambda m: m.get("type") == "response" and m.get("command") == "next",
            "stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped",
        },
        timeout_s=10.0)
    if "response" not in found and "stopped" not in found:
        print(f"FAILED [{label}]: next() neither responded nor halted within 10s - HANG", file=sys.stderr)
        return False
    description = (found.get("stopped", (None, {}))[1].get("body") or {}).get("description") or ""
    if "Could not create hardware breakpoint" in description:
        print(f"FAILED [{label}]: raw LLDB text leaked into the stopped event: {description}", file=sys.stderr)
        return False
    print(f"   OK: {label} (next() produced a response/event, no hang: {found.get('response', (None, {}))[1]})")
    return True


def case_f_startup_file_addresses_never_alias_literal_pool(client: Client, startup_s: Path) -> bool:
    """These startup_stm32h7s3xx.s lines must resolve within Reset_Handler's
    real code range, never the literal-pool range right after it."""
    label = "case f: startup file addresses avoid the literal pool"
    lines = [58, 64, 68, 71, 76, 81, 95, 97, 100]
    always_verified = {58, 64, 68}
    client.send("setBreakpoints", {"source": {"path": str(startup_s)}, "breakpoints": [{"line": ln} for ln in lines]})
    resp = client.wait_for_response("setBreakpoints")
    bps = (resp.get("body") or {}).get("breakpoints", []) if resp else []
    if len(bps) != len(lines):
        print(f"FAILED [{label}]: expected {len(lines)} breakpoints, got {bps}", file=sys.stderr)
        return False
    for req_line, bp in zip(lines, bps):
        if req_line in always_verified and not bp.get("verified"):
            print(f"FAILED [{label}]: line {req_line} expected verified, got {bp}", file=sys.stderr)
            return False
        addr = int(bp["instructionReference"], 16) if bp.get("instructionReference") else None
        if addr is not None and not (0x08001214 <= addr <= 0x0800124A):
            print(f"FAILED [{label}]: line {req_line} resolved to {bp['instructionReference']}, "
                  f"expected within Reset_Handler's real code range", file=sys.stderr)
            return False
    print(f"   OK: {label}")
    return True


def case_g_breakpoint_locations_cover_column_less_dwarf(client: Client, startup_s: Path) -> bool:
    """startup_stm32h7s3xx.s has no DWARF column info (gas-emitted assembly) -
    breakpointLocations must still return every real code line, not []."""
    label = "case g: breakpointLocations covers column-less DWARF"
    expected = {58, 59, 61, 64, 65, 66, 67, 68, 71, 72, 73, 76, 77, 78, 81, 82, 83, 84, 87, 88, 91, 92, 95, 97, 100}
    client.send("breakpointLocations", {"source": {"path": str(startup_s)}, "line": 55, "endLine": 100})
    resp = client.wait_for_response("breakpointLocations")
    lines = {b["line"] for b in (resp.get("body") or {}).get("breakpoints", [])} if resp else set()
    if not lines or not lines.issubset(expected) or not expected.issubset(lines):
        print(f"FAILED [{label}]: expected exactly {sorted(expected)}, got {sorted(lines)}", file=sys.stderr)
        return False
    print(f"   OK: {label} ({len(lines)} lines)")
    return True


def case_h_literal_pool_alias_uses_one_comparator(client: Client, startup_s: Path) -> bool:
    """Lines 58/64 each alias a literal-pool word onto the same source line;
    only the real address may consume a hardware comparator, not both."""
    label = "case h: literal-pool alias consumes one comparator, not two"
    client.send("evaluate", {"expression": "`breakpoint list -v", "context": "repl"})
    resp = client.wait_for_response("evaluate")
    result = (resp.get("body") or {}).get("result", "") if resp else ""
    for line, addr in ((58, "0x08001214"), (64, "0x0800121c")):
        block_start = result.find(f"line = {line},")
        if block_start == -1:
            print(f"FAILED [{label}]: no breakpoint listed for line {line}", file=sys.stderr)
            return False
        block_end = result.find("\n\n\n", block_start)
        block = result[block_start:block_end if block_end != -1 else None]
        if block.count("hardware = true") != 1 or addr not in block:
            print(f"FAILED [{label}]: expected exactly one hardware location at {addr} for line {line}:\n{block}",
                  file=sys.stderr)
            return False
    print(f"   OK: {label}")
    return True


def main() -> int:
    if len(sys.argv) < 5:
        print(f"usage: {sys.argv[0]} <path-to-trailer-dap> <path-to-firmware.elf> "
              f"<path-to-openocd-tcl-dir> <path-to-board-cfg> [gdb-port]", file=sys.stderr)
        return 2
    adapter_path, program_path, tcl_dir, board_cfg = sys.argv[1:5]
    gdb_port = sys.argv[5] if len(sys.argv) > 5 else "3333"
    main_c = Path(program_path).parents[3] / "Boot" / "Core" / "Src" / "main.c"

    proc = subprocess.Popen([adapter_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=sys.stderr)
    client = Client(proc)
    ok = True

    try:
        print("-> initialize")
        client.send("initialize", {"adapterID": "trailer", "clientID": "stepping_smoke_test"})
        resp = client.wait_for_response("initialize")
        if resp is None or not resp.get("success"):
            print("FAILED: initialize did not succeed", file=sys.stderr)
            return 1

        print(f"-> launch (program={program_path}, openocd.gdbPort={gdb_port})")
        client.send("launch", {
            "program": program_path,
            "openocd": {"scriptSearchDirs": [tcl_dir], "configFiles": [board_cfg], "gdbPort": gdb_port},
            "stopOnEntry": True,
        })
        if client.wait_for_all({"initialized": lambda m: m.get("type") == "event" and m.get("event") == "initialized"},
                                timeout_s=30.0).get("initialized") is None:
            print("FAILED: never saw 'initialized' event after launch", file=sys.stderr)
            return 1

        print("-> configurationDone")
        client.send("configurationDone")
        found = client.wait_for_all(
            {
                "launch": lambda m: m.get("type") == "response" and m.get("command") == "launch",
                "configurationDone": lambda m: m.get("type") == "response" and m.get("command") == "configurationDone",
            },
            timeout_s=15.0)
        if "launch" not in found or not found["launch"][1].get("success"):
            print(f"FAILED: launch did not succeed: {found.get('launch')}", file=sys.stderr)
            return 1
        if "configurationDone" not in found or not found["configurationDone"][1].get("success"):
            print(f"FAILED: configurationDone did not succeed: {found.get('configurationDone')}", file=sys.stderr)
            return 1

        thread_id = 1
        startup_s = Path(program_path).parent.parent / "startup_stm32h7s3xx.s"
        ok = case_a_step_over_hal_init(client, thread_id, main_c) and ok
        ok = case_b_step_in_hal_init(client, thread_id, main_c) and ok
        ok = case_c_step_out_of_hal_init(client, thread_id, main_c) and ok
        ok = case_d_step_through_reset_handler(client, thread_id) and ok
        ok = case_e_comparator_exhaustion_never_hangs(client, thread_id, main_c) and ok
        # Frees the FPB comparators case e occupied in main.c before f/g/h need
        # their own in the startup file.
        set_breakpoints(client, main_c, [], "cleanup")
        ok = case_f_startup_file_addresses_never_alias_literal_pool(client, startup_s) and ok
        ok = case_g_breakpoint_locations_cover_column_less_dwarf(client, startup_s) and ok
        ok = case_h_literal_pool_alias_uses_one_comparator(client, startup_s) and ok

    finally:
        print("-> killing adapter process directly (a real hang means disconnect wouldn't be serviced either)")
        proc.kill()
        try:
            proc.wait(timeout=10)
        except Exception:
            print("WARNING: adapter process did not die within 10s of SIGKILL", file=sys.stderr)

    if ok:
        print("stepping_smoke_test: OK (next/stepIn/stepOut against real hardware, "
              "including a hardware-comparator-exhaustion case)")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
