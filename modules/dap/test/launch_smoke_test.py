#!/usr/bin/env python3
"""Live hardware smoke check for engine-owned launch: launch (the engine
creates its own in-process OpenOCD from the 'openocd' config object and
connects LLDB to its in-process gdb server) -> threads -> stackTrace ->
readMemory -> disconnect. No external OpenOCD process should ever exist -
that's the property this test exists to prove, unlike attach_smoke_test.py
which targets an already-running external OpenOCD.

Usage: launch_smoke_test.py <path-to-trailer-dap> <path-to-firmware.elf>
           <path-to-openocd-tcl-dir> <path-to-board-cfg> [gdb-port]
"""

import json
import subprocess
import sys
from pathlib import Path

try:
    import psutil
except ImportError:
    psutil = None


def frame(message: dict) -> bytes:
    body = json.dumps(message).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def read_message(stream) -> dict:
    header = b""
    while not header.endswith(b"\r\n\r\n"):
        chunk = stream.read(1)
        if not chunk:
            raise EOFError("EOF while reading headers")
        header += chunk
    content_length = None
    for line in header.decode("ascii").split("\r\n"):
        if line.lower().startswith("content-length:"):
            content_length = int(line.split(":", 1)[1].strip())
    if content_length is None:
        raise ValueError(f"no Content-Length header in: {header!r}")
    body = stream.read(content_length)
    if len(body) != content_length:
        raise EOFError("EOF while reading body")
    return json.loads(body)


class Client:
    def __init__(self, proc):
        self.proc = proc
        self.seq = 0
        self.output_events: list[dict] = []
        self.all_messages: list[dict] = []

    def send(self, command: str, arguments: dict | None = None) -> int:
        self.seq += 1
        msg = {"type": "request", "seq": self.seq, "command": command}
        if arguments is not None:
            msg["arguments"] = arguments
        self.proc.stdin.write(frame(msg))
        self.proc.stdin.flush()
        return self.seq

    def read(self) -> dict:
        msg = read_message(self.proc.stdout)
        self.all_messages.append(msg)
        kind = msg.get("type")
        if kind == "event":
            print(f"  <- event {msg.get('event')}: {json.dumps(msg.get('body'))[:200]}")
            if msg.get("event") == "output":
                self.output_events.append(msg.get("body") or {})
        elif kind == "response":
            print(f"  <- response {msg.get('command')} success={msg.get('success')}: "
                  f"{json.dumps(msg.get('body'))[:200]}")
        return msg

    def wait_for_response(self, command: str, max_messages: int = 20) -> dict:
        for _ in range(max_messages):
            msg = self.read()
            if msg.get("type") == "response" and msg.get("command") == command:
                return msg
        raise TimeoutError(f"no response for '{command}' within {max_messages} messages")

    def wait_for_all(self, predicates: dict, max_messages: int = 40) -> dict:
        """Reads messages until each named predicate has matched at least
        once. Returns {name: (arrival_order, message)} so callers can assert
        on relative ordering between two events."""
        found = {}
        for i in range(max_messages):
            msg = self.read()
            for name, predicate in predicates.items():
                if name not in found and predicate(msg):
                    found[name] = (i, msg)
            if len(found) == len(predicates):
                break
        return found


def external_openocd_processes() -> list[int]:
    """Best-effort: PIDs of any 'openocd' process other than this test and
    trailer-dap itself (which links OpenOCD in-process, not as a subprocess
    named 'openocd'). Falls back to `pgrep` if psutil isn't installed."""
    if psutil is not None:
        return [p.pid for p in psutil.process_iter(["name"]) if p.info["name"] == "openocd"]
    try:
        out = subprocess.run(["pgrep", "-x", "openocd"], capture_output=True, text=True)
        return [int(pid) for pid in out.stdout.split()]
    except FileNotFoundError:
        return []


def continue_and_wait(client: Client, thread_id: int, want_trace_data: bool, max_messages: int = 60) -> dict:
    """Resumes and waits for the autonomous breakpoint halt to be reported.
    Used to depend on a 'pause' backstop sent ~300ms after 'continue' because
    autonomous breakpoint-halt detection was unproven (see
    KNOWN_ISSUE_AUTONOMOUS_HALT_DETECTION.md, now resolved: the root cause
    was gdb/lldb defaulting to software breakpoints, which silently have no
    effect on on-chip flash; tcl/target/stm32h7rx.cfg now forces
    'gdb_breakpoint_override hard'). The backstop is gone now that the real
    fix makes the breakpoint actually fire.

    Everything of interest - the 'continue' response, 'stopped', and
    (optionally) 'trailerTraceData' - is watched in one read loop via
    wait_for_all(), since a sequence of separate wait_for_response() calls
    would silently discard whichever of these arrives in between."""
    client.send("continue", {"threadId": thread_id})
    predicates = {
        "continue_resp": lambda m: m.get("type") == "response" and m.get("command") == "continue",
        "stopped": lambda m: m.get("type") == "event" and m.get("event") == "stopped",
    }
    if want_trace_data:
        predicates["trace_data"] = lambda m: m.get("type") == "event" and m.get("event") == "trailerTraceData"

    return client.wait_for_all(predicates, max_messages=max_messages)


def run_trace_checks(client: Client, program_path: str, thread_id: int) -> bool:
    """launch -> trailerTraceStatus (disabled) -> trailerTraceEnable -> hit a
    breakpoint twice, checking trailerTraceData arrives before each 'stopped'
    and that the second event's bookkeeping/gap matches the first -> disable
    stops further events. Requires the same board's dbg_h7rs_Boot.elf."""
    main_c_path = Path(program_path).parents[3] / "Boot" / "Core" / "Src" / "main.c"

    print("-> trailerTraceStatus (expect disabled)")
    client.send("trailerTraceStatus")
    resp = client.wait_for_response("trailerTraceStatus")
    if not resp.get("success") or (resp.get("body") or {}).get("enabled") is not False:
        print(f"FAILED: trailerTraceStatus did not report enabled=false before arming: {resp}", file=sys.stderr)
        return False

    print("-> trailerTraceEnable")
    client.send("trailerTraceEnable")
    resp = client.wait_for_response("trailerTraceEnable")
    if not resp.get("success") or (resp.get("body") or {}).get("enabled") is not True:
        print(f"FAILED: trailerTraceEnable did not succeed: {resp}", file=sys.stderr)
        return False

    print(f"-> setBreakpoints ({main_c_path}:106)")
    client.send("setBreakpoints", {"source": {"path": str(main_c_path)}, "breakpoints": [{"line": 106}]})
    resp = client.wait_for_response("setBreakpoints")
    breakpoints = (resp.get("body") or {}).get("breakpoints", [])
    if not resp.get("success") or not breakpoints or not breakpoints[0].get("verified"):
        print(f"FAILED: setBreakpoints did not verify: {resp}", file=sys.stderr)
        return False

    print("-> continue (1st halt)")
    found = continue_and_wait(client, thread_id, want_trace_data=True)
    if "trace_data" not in found or "stopped" not in found:
        print(f"FAILED: did not see both trailerTraceData and stopped after 1st continue: {found}", file=sys.stderr)
        return False
    if not found["continue_resp"][1].get("success"):
        print(f"FAILED: 1st continue did not succeed: {found['continue_resp'][1]}", file=sys.stderr)
        return False
    trace_order, first_event = found["trace_data"]
    stopped_order, _ = found["stopped"]
    if trace_order >= stopped_order:
        print("FAILED: trailerTraceData did not arrive before stopped on the 1st halt", file=sys.stderr)
        return False

    first_body = first_event.get("body") or {}
    instructions = first_body.get("instructions", [])
    function_blocks = first_body.get("functionBlocks", [])
    if not instructions:
        print("FAILED: 1st trailerTraceData carried no instructions", file=sys.stderr)
        return False
    if any(not inst.get("frames") for inst in instructions):
        print("FAILED: some instruction(s) in the 1st trailerTraceData did not resolve source frames",
              file=sys.stderr)
        return False
    if not any("main" in (block.get("functionName") or "").lower() for block in function_blocks):
        print(f"FAILED: no functionBlock named like 'main' in the 1st trailerTraceData: {function_blocks}",
              file=sys.stderr)
        return False
    print(f"   1st halt: {len(instructions)} instruction(s), {len(function_blocks)} function block(s), "
          f"all resolved to source")

    first_instruction_count = first_body.get("firstInstructionIndex", 0) + len(instructions)

    print("-> continue (2nd halt)")
    found = continue_and_wait(client, thread_id, want_trace_data=True)
    if "trace_data" not in found or "stopped" not in found:
        print(f"FAILED: did not see both trailerTraceData and stopped after 2nd continue: {found}", file=sys.stderr)
        return False
    if not found["continue_resp"][1].get("success"):
        print(f"FAILED: 2nd continue did not succeed: {found['continue_resp'][1]}", file=sys.stderr)
        return False
    trace_order, second_event = found["trace_data"]
    stopped_order, _ = found["stopped"]
    if trace_order >= stopped_order:
        print("FAILED: trailerTraceData did not arrive before stopped on the 2nd halt", file=sys.stderr)
        return False

    second_body = second_event.get("body") or {}
    if second_body.get("firstInstructionIndex") != first_instruction_count:
        print(f"FAILED: 2nd halt's firstInstructionIndex ({second_body.get('firstInstructionIndex')}) != "
              f"1st halt's instruction count ({first_instruction_count})", file=sys.stderr)
        return False
    if not any(gap.get("reason") == "captureBoundary" for gap in second_body.get("gaps", [])):
        print(f"FAILED: no 'captureBoundary' gap on the 2nd halt: {second_body.get('gaps')}", file=sys.stderr)
        return False
    print(f"   2nd halt: firstInstructionIndex matches, captureBoundary gap present")

    print("-> trailerTraceDisable")
    client.send("trailerTraceDisable")
    resp = client.wait_for_response("trailerTraceDisable")
    if not resp.get("success") or (resp.get("body") or {}).get("enabled") is not False:
        print(f"FAILED: trailerTraceDisable did not succeed: {resp}", file=sys.stderr)
        return False

    print("-> continue (disabled - expect no further trailerTraceData)")
    watermark = len(client.all_messages)
    found = continue_and_wait(client, thread_id, want_trace_data=False)
    if "stopped" not in found:
        print(f"FAILED: no 'stopped' event after continuing with trace disabled: {found}", file=sys.stderr)
        return False
    if not found["continue_resp"][1].get("success"):
        print(f"FAILED: 3rd continue did not succeed: {found['continue_resp'][1]}", file=sys.stderr)
        return False
    saw_trace_data = any(
        m.get("type") == "event" and m.get("event") == "trailerTraceData" for m in client.all_messages[watermark:])
    if saw_trace_data:
        print("FAILED: trailerTraceData still fired after trailerTraceDisable", file=sys.stderr)
        return False
    print("   confirmed: trailerTraceDisable stopped further trailerTraceData events")

    return True


def main() -> int:
    if len(sys.argv) < 5:
        print(f"usage: {sys.argv[0]} <path-to-trailer-dap> <path-to-firmware.elf> "
              f"<path-to-openocd-tcl-dir> <path-to-board-cfg> [gdb-port]", file=sys.stderr)
        return 2
    adapter_path, program_path, tcl_dir, board_cfg = sys.argv[1:5]
    gdb_port = sys.argv[5] if len(sys.argv) > 5 else "3333"

    proc = subprocess.Popen([adapter_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=sys.stderr)
    assert proc.stdin is not None and proc.stdout is not None
    client = Client(proc)
    ok = True
    thread_id = None

    try:
        print("-> initialize")
        client.send("initialize", {"adapterID": "trailer", "clientID": "launch_smoke_test"})
        resp = client.wait_for_response("initialize")
        ok = ok and resp.get("success", False)

        print(f"-> launch (program={program_path}, openocd.gdbPort={gdb_port})")
        client.send("launch", {
            "program": program_path,
            "openocd": {
                "scriptSearchDirs": [tcl_dir],
                "configFiles": [board_cfg],
                "gdbPort": gdb_port,
            },
            "stopOnEntry": True,
        })
        # launch's response is deferred until configurationDone (real DAP
        # semantics - see DelayedResponseRequestHandler); drain messages
        # until we've at least seen the `initialized` event that signals
        # it's safe to send configurationDone.
        for _ in range(10):
            msg = client.read()
            if msg.get("type") == "event" and msg.get("event") == "initialized":
                break
        else:
            print("FAILED: never saw 'initialized' event after launch", file=sys.stderr)
            return 1

        print("-> configurationDone")
        client.send("configurationDone")
        seen = {"launch": None, "configurationDone": None}
        for _ in range(20):
            msg = client.read()
            if msg.get("type") == "response" and msg.get("command") in seen:
                seen[msg["command"]] = msg
            if all(seen.values()):
                break
        if not seen["launch"] or not seen["launch"].get("success"):
            print(f"FAILED: launch did not succeed: {seen['launch']}", file=sys.stderr)
            ok = False
        if not seen["configurationDone"] or not seen["configurationDone"].get("success"):
            print(f"FAILED: configurationDone did not succeed: {seen['configurationDone']}", file=sys.stderr)
            ok = False

        external = external_openocd_processes()
        if external:
            print(f"FAILED: found external 'openocd' process(es) {external} - the extension/test "
                  f"harness must never spawn one, the engine owns OpenOCD in-process", file=sys.stderr)
            ok = False
        else:
            print("   confirmed: no external 'openocd' process exists")

        trace_failures = [e for e in client.output_events if "trace unavailable" in (e.get("output") or "")]
        if trace_failures:
            print(f"FAILED: DebugContext::CreateTrace() failed during launch: {trace_failures}", file=sys.stderr)
            ok = False
        else:
            print("   confirmed: core::TraceManager was created during launch (no 'trace unavailable' output)")

        print("-> threads")
        client.send("threads")
        resp = client.wait_for_response("threads")
        threads = (resp.get("body") or {}).get("threads", [])
        if not resp.get("success") or not threads:
            print(f"FAILED: threads request returned no threads: {resp}", file=sys.stderr)
            ok = False
        else:
            thread_id = threads[0]["id"]
            print(f"   real thread(s) from hardware: {threads}")

        frame_pc = None
        if thread_id is not None:
            print(f"-> stackTrace (threadId={thread_id})")
            client.send("stackTrace", {"threadId": thread_id})
            resp = client.wait_for_response("stackTrace")
            frames = (resp.get("body") or {}).get("stackFrames", [])
            if not resp.get("success") or not frames:
                print(f"FAILED: stackTrace returned no frames: {resp}", file=sys.stderr)
                ok = False
            else:
                print(f"   real frame(s) from hardware:")
                for f in frames:
                    print(f"     #{f.get('id')} {f.get('name')} {f.get('instructionPointerReference')}")
                frame_pc = frames[0].get("instructionPointerReference")

        if frame_pc is not None:
            print(f"-> readMemory (memoryReference={frame_pc}) - exercises OpenOcdMemoryStrategy")
            client.send("readMemory", {"memoryReference": frame_pc, "count": 16})
            resp = client.wait_for_response("readMemory")
            data = (resp.get("body") or {}).get("data")
            if not resp.get("success") or not data:
                print(f"FAILED: readMemory returned no data: {resp}", file=sys.stderr)
                ok = False
            else:
                print(f"   read {len(data)} base64-encoded byte(s) via OpenOCD-backed memory access")

        if thread_id is not None and ok:
            ok = run_trace_checks(client, program_path, thread_id)

        print("-> disconnect")
        client.send("disconnect")
        client.wait_for_response("disconnect")

    except (EOFError, ValueError, TimeoutError) as e:
        print(f"FAILED: {e}", file=sys.stderr)
        ok = False
    finally:
        try:
            proc.stdin.close()
        except Exception:
            pass
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    if ok:
        print("launch_smoke_test: OK (launch -> threads -> stackTrace -> readMemory against real "
              "hardware, no external OpenOCD process)")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
