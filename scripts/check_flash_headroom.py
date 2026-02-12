#!/usr/bin/env python3
"""
Validate firmware flash headroom from PlatformIO build logs.

Parses lines like:
  Flash: [==========]  98.4% (used 6445804 bytes from 6553600 bytes)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FLASH_RE = re.compile(r"Flash:\s+\[[^\]]+\]\s+([0-9.]+)%\s+\(used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes\)")
ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-9;]*m")


def parse_flash_usage(log_text: str) -> tuple[float, int, int]:
    cleaned = ANSI_ESCAPE_RE.sub("", log_text.replace("\r", ""))
    matches = FLASH_RE.findall(cleaned)
    if not matches:
        raise ValueError("Could not find flash usage line in build log")
    percent_str, used_str, total_str = matches[-1]
    return float(percent_str), int(used_str), int(total_str)


def format_kib(value: int) -> str:
    return f"{value / 1024:.1f} KiB"


def check_headroom(used: int, total: int, min_headroom_bytes: int) -> tuple[bool, int]:
    """Return (passes, headroom_bytes) for a used/total pair."""
    headroom_bytes = total - used
    return headroom_bytes >= min_headroom_bytes, headroom_bytes


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail when firmware flash headroom drops below threshold.")
    parser.add_argument("--log", required=True, type=Path, help="Path to PlatformIO build log file")
    parser.add_argument(
        "--firmware",
        type=Path,
        help="Optional path to firmware.bin; when set, also validates binary size against app partition bytes",
    )
    parser.add_argument(
        "--partition-bytes",
        type=int,
        help="Override app partition size in bytes for firmware.bin checks (default: value parsed from build log)",
    )
    parser.add_argument(
        "--min-headroom-kb",
        type=int,
        default=80,
        help="Minimum required flash headroom in KiB (default: 80)",
    )
    args = parser.parse_args()

    if not args.log.exists():
        print(f"ERROR: log file not found: {args.log}")
        return 2

    try:
        percent, used, total = parse_flash_usage(args.log.read_text(errors="ignore"))
    except ValueError as exc:
        print(f"ERROR: {exc}")
        return 2

    min_headroom_bytes = args.min_headroom_kb * 1024
    linker_ok, linker_headroom = check_headroom(used, total, min_headroom_bytes)
    ok = True

    print(
        f"Flash usage: {percent:.1f}% ({used} / {total} bytes), "
        f"headroom {linker_headroom} bytes ({format_kib(linker_headroom)})"
    )
    print(
        f"Required minimum headroom: {min_headroom_bytes} bytes "
        f"({format_kib(min_headroom_bytes)})"
    )

    if not linker_ok:
        deficit = min_headroom_bytes - linker_headroom
        print(
            "ERROR: Linker flash headroom below threshold by "
            f"{deficit} bytes ({format_kib(deficit)})."
        )
        ok = False

    if args.firmware is not None:
        if not args.firmware.exists():
            print(f"ERROR: firmware.bin not found: {args.firmware}")
            return 2

        partition_bytes = args.partition_bytes if args.partition_bytes is not None else total
        firmware_size = args.firmware.stat().st_size
        firmware_ok, firmware_headroom = check_headroom(firmware_size, partition_bytes, min_headroom_bytes)

        print(
            "Firmware image: "
            f"{firmware_size} / {partition_bytes} bytes, "
            f"headroom {firmware_headroom} bytes ({format_kib(firmware_headroom)})"
        )

        if firmware_size > partition_bytes:
            over = firmware_size - partition_bytes
            print(
                "ERROR: firmware.bin exceeds app partition by "
                f"{over} bytes ({format_kib(over)})."
            )
            ok = False
        elif not firmware_ok:
            deficit = min_headroom_bytes - firmware_headroom
            print(
                "ERROR: firmware.bin headroom below threshold by "
                f"{deficit} bytes ({format_kib(deficit)})."
            )
            ok = False

    if ok:
        print("Flash headroom check passed.")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
