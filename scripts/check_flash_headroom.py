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

FLASH_RE = re.compile(r"Flash:\s+\[[^\]]+\]\s+([0-9.]+)% \(used (\d+) bytes from (\d+) bytes\)")


def parse_flash_usage(log_text: str) -> tuple[float, int, int]:
    matches = FLASH_RE.findall(log_text)
    if not matches:
        raise ValueError("Could not find flash usage line in build log")
    percent_str, used_str, total_str = matches[-1]
    return float(percent_str), int(used_str), int(total_str)


def format_kib(value: int) -> str:
    return f"{value / 1024:.1f} KiB"


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail when firmware flash headroom drops below threshold.")
    parser.add_argument("--log", required=True, type=Path, help="Path to PlatformIO build log file")
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
    headroom_bytes = total - used

    print(
        f"Flash usage: {percent:.1f}% ({used} / {total} bytes), "
        f"headroom {headroom_bytes} bytes ({format_kib(headroom_bytes)})"
    )
    print(
        f"Required minimum headroom: {min_headroom_bytes} bytes "
        f"({format_kib(min_headroom_bytes)})"
    )

    if headroom_bytes < min_headroom_bytes:
        deficit = min_headroom_bytes - headroom_bytes
        print(
            "ERROR: Flash headroom below threshold by "
            f"{deficit} bytes ({format_kib(deficit)})."
        )
        return 1

    print("Flash headroom check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
