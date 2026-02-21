#!/usr/bin/env python3
"""
Hardware pattern checker for CrossPoint Reader firmware.

Enforces hardware-aware coding rules that guard against device-level bugs:

  RULE 1 — SPI mutex discipline:
    server->send() (and sendHeader/sendContent/send_P) must NOT be called while
    a SpiBusMutex::Guard is alive.  Pattern: capture SD result into a local bool,
    let the guard scope close, then call send outside.

  RULE 2 — Structured logging only:
    Serial.print / Serial.println / Serial.printf / Serial.write are banned in
    firmware source.  Use LOG_ERR / LOG_WRN / LOG_INF / LOG_DBG instead.

  RULE 3 — SD access via Storage wrapper:
    Direct SD.open / SD.mkdir / SD.remove / SD.exists / SD.rename / SD.rmdir
    are banned.  All SD card access must go through Storage.* (which manages the
    SPI mutex internally).

  RULE 4 — Settings persistence:
    SETTINGS.saveToFile() return value must not be silently discarded.  Assign it
    to a bool and log or handle failure.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Generator

ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT / "src"

# main.cpp uses Serial directly for the screenshot binary protocol.
SERIAL_CHECK_EXEMPT: frozenset[str] = frozenset({"main.cpp"})

# Direct SD.* method calls that should go through Storage.*
_SD_METHODS = "open|mkdir|remove|rmdir|exists|rename|openNextFile|cardBegin"
BANNED_SD = re.compile(rf"\bSD\.({_SD_METHODS})\s*\(")

# Serial output calls
BANNED_SERIAL = re.compile(r"\bSerial\.(print|println|printf|write)\s*\(")

# server->send variants that must not fire while holding the SPI mutex
SERVER_SEND = re.compile(r"\bserver->(send|sendHeader|sendContent|send_P)\s*\(")

# Unchecked SETTINGS.saveToFile() — standalone statement, result not used
UNCHECKED_SAVE = re.compile(r"^\s*SETTINGS\.saveToFile\s*\(\s*\)\s*;")


# ---------------------------------------------------------------------------
# Comment stripping
# ---------------------------------------------------------------------------


def _strip_comments(source: str) -> list[tuple[int, str]]:
    """
    Return (1-based line_number, stripped_line) pairs with C++ comments removed.
    Line numbers are preserved so error messages point to the original file.
    """
    result: list[tuple[int, str]] = []
    in_block = False

    for line_no, raw in enumerate(source.split("\n"), 1):
        line = raw

        if in_block:
            end = line.find("*/")
            if end == -1:
                result.append((line_no, ""))
                continue
            line = line[end + 2:]
            in_block = False

        # Collapse same-line /* ... */ spans (may be multiple per line).
        while True:
            start = line.find("/*")
            if start == -1:
                break
            end = line.find("*/", start + 2)
            if end == -1:
                line = line[:start]
                in_block = True
                break
            line = line[:start] + line[end + 2:]

        # Remove // line comment.  Does not parse string literals — acceptable
        # for this firmware codebase where braces inside strings are very rare.
        slash = line.find("//")
        if slash != -1:
            line = line[:slash]

        result.append((line_no, line))

    return result


# ---------------------------------------------------------------------------
# Per-rule checkers
# ---------------------------------------------------------------------------


def check_mutex_discipline(path: Path) -> list[str]:
    """
    RULE 1: server->send*() must not be called while SpiBusMutex::Guard is alive.

    Guards are tracked by brace depth.  A guard declared when depth==D remains
    alive until depth drops below D (i.e. the enclosing block closes).  Any
    server->send*() call while an active guard exists is flagged.
    """
    errors: list[str] = []
    stripped = _strip_comments(path.read_text(encoding="utf-8", errors="replace"))

    depth = 0
    # Stack entries: (declaration_line_no, depth_at_declaration)
    active: list[tuple[int, int]] = []

    for line_no, line in stripped:
        opens = line.count("{")
        closes = line.count("}")

        # Process opening braces first so that a guard on the same line as its
        # opening brace (e.g. `{ SpiBusMutex::Guard guard;`) gets the inner depth.
        depth += opens

        if "SpiBusMutex::Guard" in line:
            active.append((line_no, depth))

        if SERVER_SEND.search(line) and active:
            guard_line, _ = active[-1]
            errors.append(
                f"{path.relative_to(ROOT)}:{line_no}: "
                f"[SPI-MUTEX] server->send*() inside SpiBusMutex::Guard scope "
                f"(guard declared at line {guard_line}). "
                f"Capture the SD result into a local variable, close the guard "
                f"scope, then call server->send() outside."
            )

        depth -= closes
        if depth < 0:
            depth = 0  # guard against malformed input

        # Retire guards whose enclosing block just closed.
        # A guard at depth D is alive while current depth >= D.
        active = [(ln, d) for ln, d in active if d <= depth]

    return errors


def check_serial_usage(path: Path) -> list[str]:
    """RULE 2: Ban direct Serial.print*/printf in favour of LOG_* macros."""
    if path.name in SERIAL_CHECK_EXEMPT:
        return []

    errors: list[str] = []
    stripped = _strip_comments(path.read_text(encoding="utf-8", errors="replace"))

    for line_no, line in stripped:
        m = BANNED_SERIAL.search(line)
        if m:
            errors.append(
                f"{path.relative_to(ROOT)}:{line_no}: "
                f"[LOGGING] {m.group(0)!r} — "
                f"use LOG_ERR/LOG_WRN/LOG_INF/LOG_DBG instead."
            )

    return errors


def check_direct_sd_access(path: Path) -> list[str]:
    """RULE 3: Ban direct SD.* calls; all SD access must go through Storage.*."""
    errors: list[str] = []
    stripped = _strip_comments(path.read_text(encoding="utf-8", errors="replace"))

    for line_no, line in stripped:
        m = BANNED_SD.search(line)
        if m:
            errors.append(
                f"{path.relative_to(ROOT)}:{line_no}: "
                f"[SD-ACCESS] {m.group(0)!r} — "
                f"use Storage.* wrapper; it manages SpiBusMutex internally."
            )

    return errors


def check_unchecked_settings_save(path: Path) -> list[str]:
    """RULE 4: SETTINGS.saveToFile() return value must not be silently discarded."""
    errors: list[str] = []
    stripped = _strip_comments(path.read_text(encoding="utf-8", errors="replace"))

    for line_no, line in stripped:
        if UNCHECKED_SAVE.search(line):
            errors.append(
                f"{path.relative_to(ROOT)}:{line_no}: "
                f"[SETTINGS] SETTINGS.saveToFile() return value discarded — "
                f"assign to bool and log or handle failure."
            )

    return errors


# ---------------------------------------------------------------------------
# File discovery and entry point
# ---------------------------------------------------------------------------


def _source_files(root: Path) -> Generator[Path, None, None]:
    for pattern in ("**/*.cpp", "**/*.h"):
        yield from sorted(root.glob(pattern))


def main() -> int:
    errors: list[str] = []
    checked = 0

    for src in _source_files(SRC_DIR):
        errors.extend(check_mutex_discipline(src))
        errors.extend(check_serial_usage(src))
        errors.extend(check_direct_sd_access(src))
        errors.extend(check_unchecked_settings_save(src))
        checked += 1

    if errors:
        print(f"Hardware pattern check FAILED ({len(errors)} violation(s) across {checked} files):")
        for e in errors:
            print(f"  {e}")
        return 1

    print(f"Hardware pattern check passed: {checked} source files clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
