#!/usr/bin/env python3
"""Live hardware smoke check for the peripheral DAP surface: launch (the
engine's own embedded OpenOCD, not an externally-run process - see
launch_smoke_test.py) -> read the device's peripheral list -> read GPIOA ->
write+read-back a scratch register (TAMP_BKP0R - backup-domain user storage,
has no effect on board behavior, unlike a GPIO pin whose wiring is
board-specific) -> start a peripheral watch and confirm it delivers
monotonically increasing frames -> confirm a write mid-poll never surfaces a
frame tagged with the pre-write epoch -> disconnect.

Peripheral watches poll through OpenOcdProvider directly, which only exists
under `launch` (TargetManager::Launch calls CreateOpenOcd(); attach never
does) - so this must launch, not attach, to exercise that code path at all.

Usage: peripheral_smoke_test.py <path-to-trailer-dap> <path-to-firmware.elf>
           <path-to-openocd-tcl-dir> <path-to-board-cfg> <device-name>
           <path-to-device-svd> [gdb-port]
"""

import json
import subprocess
import sys
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
                  f"{json.dumps(msg.get('body'))[:300]}")
        return msg

    def request(self, command: str, arguments: dict | None = None, max_messages: int = 20) -> dict:
        self.send(command, arguments)
        for _ in range(max_messages):
            msg = self.read()
            if msg.get("type") == "response" and msg.get("command") == command:
                return msg
        raise TimeoutError(f"no response for '{command}' within {max_messages} messages")


def find_register(peripheral_detail: dict, name: str) -> dict | None:
    for reg in peripheral_detail.get("registers", []):
        if reg.get("name") == name:
            return reg
    return None


def main() -> int:
    if len(sys.argv) < 7:
        print(f"usage: {sys.argv[0]} <path-to-trailer-dap> <path-to-firmware.elf> "
              f"<path-to-openocd-tcl-dir> <path-to-board-cfg> <device-name> "
              f"<path-to-device-svd> [gdb-port]", file=sys.stderr)
        return 2
    adapter_path, program_path, tcl_dir, board_cfg, device_name, svd_path = sys.argv[1:7]
    gdb_port = sys.argv[7] if len(sys.argv) > 7 else "3333"

    proc = subprocess.Popen([adapter_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=sys.stderr)
    assert proc.stdin is not None and proc.stdout is not None
    client = Client(proc)
    ok = True

    try:
        print("-> initialize")
        resp = client.request("initialize", {"adapterID": "trailer", "clientID": "peripheral_smoke_test"})
        ok = ok and resp.get("success", False)

        print(f"-> launch (program={program_path}, openocd.gdbPort={gdb_port}, "
              f"deviceName={device_name})")
        client.send("launch", {
            "program": program_path,
            "openocd": {
                "scriptSearchDirs": [tcl_dir],
                "configFiles": [board_cfg],
                "gdbPort": gdb_port,
            },
            "deviceName": device_name,
            "svdPath": svd_path,
        })
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
            return 1
        if not seen["configurationDone"] or not seen["configurationDone"].get("success"):
            print(f"FAILED: configurationDone did not succeed: {seen['configurationDone']}", file=sys.stderr)
            return 1

        # One-shot peripheral reads/writes go through the same halt-gated
        # path as readMemory/writeMemory (regular process memory access) -
        # only the watch is designed to bypass halt state. Pause first.
        print("-> threads")
        resp = client.request("threads")
        threads = (resp.get("body") or {}).get("threads", [])
        if not resp.get("success") or not threads:
            print(f"FAILED: threads request returned no threads: {resp}", file=sys.stderr)
            return 1
        thread_id = threads[0]["id"]

        print(f"-> pause (threadId={thread_id})")
        client.send("pause", {"threadId": thread_id})
        for _ in range(20):
            msg = client.read()
            if msg.get("type") == "event" and msg.get("event") == "stopped":
                break
        else:
            print("FAILED: no 'stopped' event after pause", file=sys.stderr)
            return 1

        print("-> trailerDeviceInfo")
        resp = client.request("trailerDeviceInfo")
        body = resp.get("body") or {}
        if not resp.get("success") or not body.get("peripherals"):
            print(f"FAILED: trailerDeviceInfo returned no peripherals: {resp}", file=sys.stderr)
            ok = False
        else:
            print(f"   device={body.get('deviceName')} core={body.get('core')} "
                  f"peripherals={len(body['peripherals'])} memoryRegions={len(body.get('memoryRegions', []))} "
                  f"corePeripherals={len(body.get('corePeripherals', []))}")

        print("-> trailerPeripheralDetail (GPIOA)")
        resp = client.request("trailerPeripheralDetail", {"peripheral": "GPIOA"})
        gpioa = resp.get("body") or {}
        if not resp.get("success") or not gpioa.get("registers"):
            print(f"FAILED: trailerPeripheralDetail(GPIOA) failed: {resp}", file=sys.stderr)
            ok = False

        print("-> trailerPeripheralRead (GPIOA, safeOnly)")
        resp = client.request("trailerPeripheralRead", {"peripheral": "GPIOA", "safeOnly": True})
        if not resp.get("success"):
            print(f"FAILED: trailerPeripheralRead(GPIOA) failed: {resp}", file=sys.stderr)
            ok = False
        else:
            values = (resp.get("body") or {}).get("registers", [])
            print(f"   read {len(values)} safe register(s) from real GPIOA hardware")

        # NVIC (core-SVD peripheral, not device-SVD) - detail/read only, no
        # write, since core peripheral registers can affect real system
        # behavior (e.g. SCB) in ways no device peripheral does.
        print("-> trailerPeripheralDetail (NVIC, core)")
        resp = client.request("trailerPeripheralDetail", {"peripheral": "NVIC", "core": True})
        nvic = resp.get("body") or {}
        if not resp.get("success") or not nvic.get("registers"):
            print(f"FAILED: trailerPeripheralDetail(NVIC, core) failed: {resp}", file=sys.stderr)
            ok = False

        print("-> trailerPeripheralRead (NVIC, core, safeOnly)")
        resp = client.request("trailerPeripheralRead", {"peripheral": "NVIC", "safeOnly": True, "core": True})
        if not resp.get("success"):
            print(f"FAILED: trailerPeripheralRead(NVIC, core) failed: {resp}", file=sys.stderr)
            ok = False
        else:
            values = (resp.get("body") or {}).get("registers", [])
            print(f"   read {len(values)} safe register(s) from real NVIC hardware")

        # TAMP_BKP0R is backup-domain user scratch storage - writing it has
        # no effect on board behavior, unlike a GPIO pin whose wiring is
        # board-specific and whose "safe" pin isn't knowable from the SVD
        # alone. This is the write/read-back verification target.
        print("-> trailerPeripheralDetail (TAMP)")
        resp = client.request("trailerPeripheralDetail", {"peripheral": "TAMP"})
        tamp = resp.get("body") or {}
        bkp0 = find_register(tamp, "TAMP_BKP0R")
        if not resp.get("success") or not bkp0:
            print(f"FAILED: TAMP_BKP0R not found: {resp}", file=sys.stderr)
            return 1

        test_value = 0xC0FFEE00
        print(f"-> trailerPeripheralWrite (TAMP::TAMP_BKP0R = 0x{test_value:08X})")
        resp = client.request("trailerPeripheralWrite",
                               {"peripheral": "TAMP", "register": "TAMP_BKP0R", "value": test_value})
        readback = (resp.get("body") or {}).get("value")
        if not resp.get("success") or readback != test_value:
            print(f"FAILED: write/read-back mismatch: wrote 0x{test_value:08X}, got {readback}", file=sys.stderr)
            ok = False
        else:
            print(f"   read-back confirmed: 0x{readback:08X}")

        print("-> trailerPeripheralWatchStart (TAMP, 200ms)")
        resp = client.request("trailerPeripheralWatchStart",
                               {"peripheral": "TAMP", "intervalMs": 200, "safeOnly": True})
        watch_id = (resp.get("body") or {}).get("watchId")
        if not resp.get("success") or watch_id is None:
            print(f"FAILED: trailerPeripheralWatchStart failed: {resp}", file=sys.stderr)
            ok = False
            watch_id = None

        if watch_id is not None:
            frames = []
            deadline = time.time() + 3
            while len(frames) < 2 and time.time() < deadline:
                msg = client.read()
                if (msg.get("type") == "event" and msg.get("event") == "trailerPeripheralData"
                        and msg.get("body", {}).get("watchId") == watch_id):
                    frames.append(msg["body"])
            if len(frames) < 2:
                print(f"FAILED: only {len(frames)} trailerPeripheralData frame(s) in 3s", file=sys.stderr)
                ok = False
            elif frames[1]["sequence"] <= frames[0]["sequence"]:
                print(f"FAILED: sequence not monotonic: {frames[0]['sequence']} -> {frames[1]['sequence']}",
                      file=sys.stderr)
                ok = False
            else:
                print(f"   {len(frames)} frames observed, sequence {frames[0]['sequence']} -> "
                      f"{frames[1]['sequence']}, epoch={frames[-1]['epoch']}")

            # Stale-frame suppression: write while the watch is live, then
            # confirm no delivered frame is tagged with a pre-write epoch.
            pre_write_epoch = frames[-1]["epoch"] if frames else None
            print("-> trailerPeripheralWrite during active watch (epoch bump check)")
            resp = client.request("trailerPeripheralWrite",
                                   {"peripheral": "TAMP", "register": "TAMP_BKP0R", "value": test_value ^ 0xFF})
            if not resp.get("success"):
                print(f"FAILED: write during watch failed: {resp}", file=sys.stderr)
                ok = False

            post_write_frames = []
            deadline = time.time() + 3
            while len(post_write_frames) < 2 and time.time() < deadline:
                msg = client.read()
                if (msg.get("type") == "event" and msg.get("event") == "trailerPeripheralData"
                        and msg.get("body", {}).get("watchId") == watch_id):
                    post_write_frames.append(msg["body"])
                    if pre_write_epoch is not None and msg["body"]["epoch"] == pre_write_epoch:
                        print(f"FAILED: frame after write still carries pre-write epoch {pre_write_epoch}",
                              file=sys.stderr)
                        ok = False
            if not post_write_frames:
                print("FAILED: no frames observed after the write", file=sys.stderr)
                ok = False
            else:
                print(f"   {len(post_write_frames)} post-write frame(s), no stale epoch observed")

            print("-> trailerPeripheralWatchStop")
            resp = client.request("trailerPeripheralWatchStop", {"watchId": watch_id})
            if not resp.get("success"):
                print(f"FAILED: trailerPeripheralWatchStop failed: {resp}", file=sys.stderr)
                ok = False

        # Leave the scratch register zeroed rather than at a magic test value.
        client.request("trailerPeripheralWrite", {"peripheral": "TAMP", "register": "TAMP_BKP0R", "value": 0})

        print("-> disconnect")
        client.request("disconnect")

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
        print("peripheral_smoke_test: OK (launch -> device info -> peripheral read/write/watch against real hardware)")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
