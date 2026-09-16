"""Wires System CTI's ETFFULL to the M7 CTI's EDBGRQ, for a real halt on buffer-full."""

from dap_client import telnet_command

SYS_CTI = "stm32h7x.cti_sys"
M7_CTI = "stm32h7x.cti_m7"


def raw_commands(dap_name: str = "stm32h7x.dap") -> list:
    return [
        f"cti create {SYS_CTI} -dap {dap_name} -ap-num 0 -baseaddr 0xE00F1000",
        f"cti create {M7_CTI} -dap {dap_name} -ap-num 1 -baseaddr 0xE0043000",
    ]


def cti(obj: str, cmd: str, port: int) -> str:
    return telnet_command(f"{obj} {cmd}", port=port).strip()


def ack(obj: str, port: int) -> None:
    cti(obj, "write INACK 0x01", port)
    cti(obj, "write INACK 0x00", port)


def arm(port: int) -> None:
    cti(SYS_CTI, "write GATE 0xF", port)
    cti(SYS_CTI, "write INEN2 0x1", port)
    cti(SYS_CTI, "enable on", port)
    cti(M7_CTI, "write GATE 0xF", port)
    cti(M7_CTI, "write OUTEN0 0x1", port)
    cti(M7_CTI, "enable on", port)
    ack(SYS_CTI, port)
    ack(M7_CTI, port)


def cleanup(port: int) -> None:
    """A stuck EDBGRQ left armed can block a later flash write until the board is power-cycled."""
    for obj in (SYS_CTI, M7_CTI):
        for reg in ("INEN0", "INEN1", "INEN2", "OUTEN0", "OUTEN1", "OUTEN2", "GATE"):
            cti(obj, f"write {reg} 0x0", port)
        ack(obj, port)
        cti(obj, "enable off", port)
