"""Shared DAP client for the trace validation campaign scripts."""

import base64
import json
import queue
import socket
import struct
import subprocess
import sys
import threading
import time


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
        self.all_messages: list[dict] = []
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

    def read_one(self, timeout_s: float) -> dict | None:
        try:
            msg = self.q.get(timeout=timeout_s)
        except queue.Empty:
            return None
        if isinstance(msg, Exception):
            return None
        self.all_messages.append(msg)
        return msg

    def wait_for_response(self, command: str, timeout_s: float = 15.0) -> dict | None:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            msg = self.read_one(deadline - time.monotonic())
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
            msg = self.read_one(deadline - time.monotonic())
            if msg is None:
                break
            for name, predicate in predicates.items():
                if name not in found and predicate(msg):
                    found[name] = (i, msg)
            i += 1
        return found

    def events(self, name: str) -> list[dict]:
        return [m for m in self.all_messages if m.get("type") == "event" and m.get("event") == name]


def spawn(adapter_path: str, stderr_path: str | None = None) -> Client:
    stderr = open(stderr_path, "wb") if stderr_path else sys.stderr
    proc = subprocess.Popen([adapter_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr)
    assert proc.stdin is not None and proc.stdout is not None
    return Client(proc)


def launch(client: Client, program_path: str, tcl_dir: str, board_cfg: str, gdb_port: str = "3333",
           openocd_log_file: str | None = None, raw_commands: list[str] | None = None,
           extra_openocd: dict | None = None, extra_config: dict | None = None,
           stop_on_entry: bool = True, timeout_s: float = 30.0) -> tuple[bool, dict, dict]:
    client.send("initialize", {"adapterID": "trailer", "clientID": "campaign"})
    init_resp = client.wait_for_response("initialize", timeout_s=timeout_s)
    if init_resp is None or not init_resp.get("success"):
        return False, init_resp or {}, {}

    openocd_cfg = {"scriptSearchDirs": [tcl_dir], "configFiles": [board_cfg], "gdbPort": gdb_port}
    if openocd_log_file:
        openocd_cfg["logFile"] = openocd_log_file
    if raw_commands:
        openocd_cfg["rawCommands"] = raw_commands
    if extra_openocd:
        openocd_cfg.update(extra_openocd)

    launch_args = {"program": program_path, "openocd": openocd_cfg, "stopOnEntry": stop_on_entry}
    if extra_config:
        launch_args.update(extra_config)

    client.send("launch", launch_args)
    deadline = time.monotonic() + timeout_s
    saw_initialized = False
    while time.monotonic() < deadline:
        msg = client.read_one(deadline - time.monotonic())
        if msg is None:
            break
        if msg.get("type") == "event" and msg.get("event") == "initialized":
            saw_initialized = True
            break
    if not saw_initialized:
        return False, {}, {}

    client.send("configurationDone")
    seen = {"launch": None, "configurationDone": None}
    while time.monotonic() < deadline and not all(seen.values()):
        msg = client.read_one(deadline - time.monotonic())
        if msg is None:
            break
        if msg.get("type") == "response" and msg.get("command") in seen:
            seen[msg["command"]] = msg

    ok = bool(seen["launch"] and seen["launch"].get("success") and
              seen["configurationDone"] and seen["configurationDone"].get("success"))
    return ok, seen["launch"] or {}, seen["configurationDone"] or {}


def tid_from_threads(client: Client) -> int:
    client.send("threads")
    resp = client.wait_for_response("threads", timeout_s=10.0)
    return (resp.get("body") or {}).get("threads", [{}])[0].get("id")


def disconnect(client: Client, timeout_s: float = 10.0) -> None:
    client.send("disconnect")
    client.wait_for_response("disconnect", timeout_s=timeout_s)


def telnet_command(command: str, port: int = 4444, host: str = "127.0.0.1", timeout_s: float = 5.0,
                    idle_s: float = 0.2) -> str:
    """Sends one Tcl command over a fresh raw TCP connection; stops once the socket goes idle."""
    with socket.create_connection((host, port), timeout=timeout_s) as sock:
        sock.settimeout(timeout_s)
        sock.sendall((command + "\n").encode("ascii"))
        chunks = []
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if chunks:
                sock.settimeout(idle_s)
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            chunks.append(chunk)
        return b"".join(chunks).decode("ascii", errors="replace")


def read_memory_u32(client: Client, address: int, timeout_s: float = 10.0) -> int | None:
    """Reads one 32-bit little-endian word via readMemory, or None on failure."""
    client.send("readMemory", {"memoryReference": f"0x{address:x}", "offset": 0, "count": 4})
    resp = client.wait_for_response("readMemory", timeout_s=timeout_s)
    if resp is None or not resp.get("success"):
        return None
    data_b64 = (resp.get("body") or {}).get("data")
    if not data_b64:
        return None
    raw = base64.b64decode(data_b64)
    if len(raw) < 4:
        return None
    return struct.unpack("<I", raw[:4])[0]


def write_memory_u32(client: Client, address: int, value: int, timeout_s: float = 10.0) -> bool:
    data_b64 = base64.b64encode(struct.pack("<I", value)).decode("ascii")
    client.send("writeMemory", {"memoryReference": f"0x{address:x}", "offset": 0, "data": data_b64})
    resp = client.wait_for_response("writeMemory", timeout_s=timeout_s)
    return bool(resp and resp.get("success"))


# Cortex-M7 debug/trace register addresses used by several campaign scripts.
DWT_CTRL = 0xE0001000
DWT_CYCCNT = 0xE0001004
DEMCR = 0xE000EDFC
DEMCR_TRCENA = 1 << 24
DWT_CTRL_CYCCNTENA = 1 << 0

CFSR = 0xE000ED28
HFSR = 0xE000ED2C
ICSR = 0xE000ED04


def ensure_dwt_cycle_counter_enabled(client: Client) -> bool:
    """Sets DEMCR.TRCENA and DWT_CTRL.CYCCNTENA if not already set."""
    demcr = read_memory_u32(client, DEMCR)
    if demcr is None:
        return False
    if not (demcr & DEMCR_TRCENA):
        if not write_memory_u32(client, DEMCR, demcr | DEMCR_TRCENA):
            return False

    ctrl = read_memory_u32(client, DWT_CTRL)
    if ctrl is None:
        return False
    if not (ctrl & DWT_CTRL_CYCCNTENA):
        if not write_memory_u32(client, DWT_CTRL, ctrl | DWT_CTRL_CYCCNTENA):
            return False
    return True
