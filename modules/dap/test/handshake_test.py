#!/usr/bin/env python3
"""Deterministic smoke check for trailer-dap's DAP handshake.

Frames a real `initialize` request (Content-Length-delimited JSON, per the
DAP/LSP wire format - see dap/transport.hpp), pipes it into a trailer-dap
subprocess over stdin, and asserts on stdout a `response` for command
"initialize" with success == true and a `body` (the adapter's Capabilities,
however minimal).

Per real DAP semantics (matching the forked lldb-dap AttachRequestHandler's
DelayedResponseRequestHandler flow - see project-dap-layer-fork-strategy
memory), `initialized` is sent once `attach`/`launch` has done its
synchronous work, not right after `initialize` - so this test doesn't wait
for it. That earlier (Stage 2) placeholder behavior, where the Orchestrator
sent `initialized` immediately after `initialize`, is gone now that
DebugService is the real forked implementation.

No LLDB target/board interaction happens here - this only exercises the
transport framing, protocol (de)serialization, and Orchestrator->DebugService
dispatch, independent of any real debug session. Requires no board.

Usage: handshake_test.py <path-to-trailer-dap>
"""

import json
import subprocess
import sys


def frame(message: dict) -> bytes:
    body = json.dumps(message).encode("utf-8")
    header = f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
    return header + body


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


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <path-to-trailer-dap>", file=sys.stderr)
        return 2
    adapter_path = sys.argv[1]

    proc = subprocess.Popen(
        [adapter_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=sys.stderr,
    )
    assert proc.stdin is not None and proc.stdout is not None

    request = {
        "type": "request",
        "seq": 1,
        "command": "initialize",
        "arguments": {
            "adapterID": "trailer",
            "clientID": "handshake_test",
            "linesStartAt1": True,
            "columnsStartAt1": True,
            "pathFormat": "path",
        },
    }
    proc.stdin.write(frame(request))
    proc.stdin.flush()

    try:
        response = read_message(proc.stdout)
    except (EOFError, ValueError) as e:
        proc.kill()
        print(f"handshake_test: FAILED reading adapter output: {e}", file=sys.stderr)
        return 1

    ok = True

    if response.get("type") != "response" or response.get("command") != "initialize":
        print(f"handshake_test: FAILED expected an 'initialize' response, got: {response}", file=sys.stderr)
        ok = False
    elif not response.get("success"):
        print(f"handshake_test: FAILED initialize response reported failure: {response}", file=sys.stderr)
        ok = False
    elif "body" not in response:
        print(f"handshake_test: FAILED initialize response has no body/capabilities: {response}", file=sys.stderr)
        ok = False
    elif not response["body"].get("supportsTraceRequests"):
        print(f"handshake_test: FAILED initialize response did not report supportsTraceRequests: {response}",
              file=sys.stderr)
        ok = False

    # No session has been launched, so trailerTraceEnable/Disable/Status are
    # expected to fail (no core::TraceManager) - the property under test is
    # that they're recognized commands at all, not "unrecognized request".
    if ok:
        for seq, command in enumerate(("trailerTraceEnable", "trailerTraceDisable", "trailerTraceStatus"), start=2):
            proc.stdin.write(frame({"type": "request", "seq": seq, "command": command}))
            proc.stdin.flush()
            try:
                trace_response = read_message(proc.stdout)
            except (EOFError, ValueError) as e:
                print(f"handshake_test: FAILED reading response for '{command}': {e}", file=sys.stderr)
                ok = False
                break
            if trace_response.get("type") != "response" or trace_response.get("command") != command:
                print(f"handshake_test: FAILED expected a '{command}' response, got: {trace_response}",
                      file=sys.stderr)
                ok = False
                break
            if "unrecognized request" in trace_response.get("message", ""):
                print(f"handshake_test: FAILED '{command}' was not recognized: {trace_response}", file=sys.stderr)
                ok = False
                break

    proc.stdin.close()
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()

    if ok:
        print("handshake_test: OK (initialize -> capabilities -> trace commands recognized)")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
