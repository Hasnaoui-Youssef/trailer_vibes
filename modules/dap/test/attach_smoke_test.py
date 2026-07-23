#!/usr/bin/env python3
"""Live hardware smoke check for the Orchestrator->DebugService path: attach
(via GDB remote to a running OpenOCD instance) -> threads -> stackTrace ->
disconnect. Requires OpenOCD already running with a gdb server on the given
port, attached to a real target (see Stage 3's plan verify step).

Usage: attach_smoke_test.py <path-to-trailer-dap> <path-to-firmware.elf> [gdb-remote-port]
"""

import json
import subprocess
import sys


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
        kind = msg.get("type")
        if kind == "event":
            print(f"  <- event {msg.get('event')}: {json.dumps(msg.get('body'))[:200]}")
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


def main() -> int:
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} <path-to-trailer-dap> <path-to-firmware.elf> [gdb-remote-port]", file=sys.stderr)
        return 2
    adapter_path, program_path = sys.argv[1], sys.argv[2]
    gdb_remote_port = int(sys.argv[3]) if len(sys.argv) > 3 else 3333

    proc = subprocess.Popen([adapter_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=sys.stderr)
    assert proc.stdin is not None and proc.stdout is not None
    client = Client(proc)
    ok = True

    try:
        print("-> initialize")
        client.send("initialize", {"adapterID": "trailer", "clientID": "attach_smoke_test"})
        resp = client.wait_for_response("initialize")
        ok = ok and resp.get("success", False)

        print(f"-> attach (program={program_path}, gdb-remote-port={gdb_remote_port})")
        client.send("attach", {
            "program": program_path,
            "gdb-remote-port": gdb_remote_port,
            "stopOnEntry": True,
        })
        # attach's response is deferred until configurationDone (real DAP
        # semantics - see DelayedResponseRequestHandler); drain messages
        # until we've at least seen the `initialized` event that signals
        # it's safe to send configurationDone.
        for _ in range(10):
            msg = client.read()
            if msg.get("type") == "event" and msg.get("event") == "initialized":
                break
        else:
            print("FAILED: never saw 'initialized' event after attach", file=sys.stderr)
            return 1

        print("-> configurationDone")
        client.send("configurationDone")
        # This unblocks the deferred attach response and configurationDone's
        # own response, in either order, plus assorted events (stopped, etc).
        seen = {"attach": None, "configurationDone": None}
        for _ in range(20):
            msg = client.read()
            if msg.get("type") == "response" and msg.get("command") in seen:
                seen[msg["command"]] = msg
            if all(seen.values()):
                break
        if not seen["attach"] or not seen["attach"].get("success"):
            print(f"FAILED: attach did not succeed: {seen['attach']}", file=sys.stderr)
            ok = False
        if not seen["configurationDone"] or not seen["configurationDone"].get("success"):
            print(f"FAILED: configurationDone did not succeed: {seen['configurationDone']}", file=sys.stderr)
            ok = False

        print("-> threads")
        client.send("threads")
        resp = client.wait_for_response("threads")
        threads = (resp.get("body") or {}).get("threads", [])
        if not resp.get("success") or not threads:
            print(f"FAILED: threads request returned no threads: {resp}", file=sys.stderr)
            ok = False
            thread_id = None
        else:
            thread_id = threads[0]["id"]
            print(f"   real thread(s) from hardware: {threads}")

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

        # Event thread check: `continue` puts the process in the running
        # state; `pause` asks it to halt again. The `stopped` event that
        # follows is generated asynchronously by the event thread reacting
        # to the process's state change (SBListener -> HandleProcessEvent ->
        # SendThreadStoppedEvent) - it is not part of either request's own
        # response. Seeing it here (vs. hanging, which is what happened
        # before DebugService::StartEventThread was wired up) confirms the
        # async path independent of the request/response handlers already
        # proven above.
        print("-> continue")
        client.send("continue", {"threadId": thread_id})
        client.wait_for_response("continue")

        print("-> pause")
        client.send("pause", {"threadId": thread_id})
        client.wait_for_response("pause")

        saw_stopped = False
        for _ in range(20):
            msg = client.read()
            if msg.get("type") == "event" and msg.get("event") == "stopped":
                saw_stopped = True
                break
        if not saw_stopped:
            print("FAILED: no proactive 'stopped' event after pause (event thread not running?)", file=sys.stderr)
            ok = False

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
        print("attach_smoke_test: OK (attach -> threads -> stackTrace against real hardware)")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
