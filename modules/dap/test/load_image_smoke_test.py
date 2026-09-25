#!/usr/bin/env python3
"""Flash the first three validation workloads through launch/loadImage.

Each workload gets a fresh adapter session. Read the programmed flash back
through DAP readMemory and compare it with the matching build/*.bin image.

Usage: python3 modules/dap/test/load_image_smoke_test.py [--adapter PATH]
"""

import argparse
import base64
import subprocess
import sys
import tempfile
from pathlib import Path


ENGINE_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ENGINE_ROOT / "validation" / "campaign"))

from dap_client import disconnect, launch, spawn  # noqa: E402


WORKLOADS = ("w1_baseline", "w2_callchain", "w3_branchdense")
FLASH_BASE = 0x08000000  # validation/firmware/common/stm32h7s3xx_flash.ld
READ_SIZE = 256


def read_flash(client, address: int, count: int) -> bytes:
    client.send("readMemory", {"memoryReference": f"0x{address:x}", "count": count})
    response = client.wait_for_response("readMemory", timeout_s=15.0)
    if response is None or not response.get("success"):
        raise RuntimeError(f"readMemory at 0x{address:08x} failed: {response}")
    body = response.get("body") or {}
    if body.get("unreadableBytes", 0):
        raise RuntimeError(f"readMemory at 0x{address:08x} returned unreadable bytes: {body}")
    data = base64.b64decode(body.get("data", ""), validate=True)
    if len(data) != count:
        raise RuntimeError(f"readMemory at 0x{address:08x} returned {len(data)} of {count} bytes")
    return data


def check_image(name: str, expected: bytes, adapter: Path, tcl_dir: Path, board_cfg: Path,
                gdb_port: str) -> None:
    elf = ENGINE_ROOT / "validation" / "firmware" / name / "build" / f"{name}.elf"
    print(f"-> {name}: launch loadImage ({elf})", flush=True)
    client = spawn(str(adapter))
    try:
        log_file = Path(tempfile.gettempdir()) / f"trailer-load-image-{name}.log"
        ok, launch_response, config_response = launch(
            client, str(elf), str(tcl_dir), str(board_cfg), gdb_port=gdb_port,
            openocd_log_file=str(log_file), extra_config={"loadImage": True},
            stop_on_entry=True, timeout_s=120.0)
        if not ok:
            output = [m.get("body", {}).get("output", "") for m in client.all_messages
                      if m.get("type") == "event" and m.get("event") == "output"]
            raise RuntimeError(f"launch failed: {launch_response}, configurationDone={config_response}, "
                               f"output={output[-5:]}, OpenOCD log={log_file}")

        for offset in range(0, len(expected), READ_SIZE):
            chunk = expected[offset:offset + READ_SIZE]
            actual = read_flash(client, FLASH_BASE + offset, len(chunk))
            if actual != chunk:
                first = next(i for i, (got, want) in enumerate(zip(actual, chunk)) if got != want)
                address = FLASH_BASE + offset + first
                raise RuntimeError(f"flash mismatch at 0x{address:08x}: "
                                   f"got 0x{actual[first]:02x}, expected 0x{chunk[first]:02x}")
        print(f"   OK: {len(expected)} flash bytes match {name}.bin", flush=True)
    finally:
        if client.proc.poll() is None:
            try:
                disconnect(client, timeout_s=10.0)
            finally:
                if client.proc.poll() is None:
                    client.proc.terminate()
        try:
            client.proc.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            client.proc.kill()
            client.proc.wait()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adapter", type=Path, default=ENGINE_ROOT / "build" / "trailer-dap")
    parser.add_argument("--tcl-dir", type=Path, default=ENGINE_ROOT / "external" / "openocd" / "tcl")
    parser.add_argument("--board-cfg", type=Path,
                        default=ENGINE_ROOT / "external" / "openocd" / "tcl" / "board" / "st_nucleo_h7s.cfg")
    parser.add_argument("--gdb-port", default="3333")
    args = parser.parse_args()

    images = []
    for name in WORKLOADS:
        build = ENGINE_ROOT / "validation" / "firmware" / name / "build"
        elf = build / f"{name}.elf"
        binary = build / f"{name}.bin"
        if not elf.is_file() or not binary.is_file():
            parser.error(f"missing firmware build artifacts for {name}: {elf}, {binary}")
        images.append((name, binary.read_bytes()))
    if any(not data for _, data in images) or len({data for _, data in images}) != len(images):
        parser.error("expected three nonempty, distinct firmware binaries")

    try:
        for name, expected in images:
            check_image(name, expected, args.adapter.resolve(), args.tcl_dir.resolve(),
                        args.board_cfg.resolve(), args.gdb_port)
    except (OSError, RuntimeError) as error:
        print(f"FAILED: {error}", file=sys.stderr)
        return 1
    print("OK: all three firmware images were loaded and read back through DAP")
    return 0


if __name__ == "__main__":
    sys.exit(main())
