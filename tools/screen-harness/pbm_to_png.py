#!/usr/bin/env python3
"""Convert binary PBM (P4) images to PNG.

This script is used by CI to publish screen harness outputs for the web preview.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Iterator

from PIL import Image


def _token_stream(raw: bytes) -> Iterator[bytes]:
    i = 0
    n = len(raw)

    while i < n:
        # Skip whitespace/comments between tokens.
        while i < n and raw[i] in b" \t\r\n":
            i += 1
        if i >= n:
            break

        if raw[i] == ord("#"):
            while i < n and raw[i] != ord("\n"):
                i += 1
            continue

        start = i
        while i < n and raw[i] not in b" \t\r\n":
            i += 1
        yield raw[start:i]


def parse_pbm(path: Path) -> tuple[int, int, bytes]:
    raw = path.read_bytes()
    if not raw.startswith(b"P4"):
        raise ValueError(f"{path} is not a binary PBM (P4)")

    # Skip "P4" and parse width/height tokens from remaining bytes.
    header_payload = raw[2:]
    tokens = _token_stream(header_payload)

    width = int(next(tokens))
    height = int(next(tokens))

    # Find binary payload start: after the third whitespace run following magic/width/height.
    # Safer approach: parse linearly from start with comment handling.
    i = 0
    seen_tokens = 0
    while i < len(raw) and seen_tokens < 3:
        while i < len(raw) and raw[i] in b" \t\r\n":
            i += 1
        if i < len(raw) and raw[i] == ord("#"):
            while i < len(raw) and raw[i] != ord("\n"):
                i += 1
            continue
        while i < len(raw) and raw[i] not in b" \t\r\n":
            i += 1
        seen_tokens += 1

    while i < len(raw) and raw[i] in b" \t\r\n":
        i += 1

    row_bytes = (width + 7) // 8
    expected = row_bytes * height
    payload = raw[i : i + expected]
    if len(payload) != expected:
        raise ValueError(f"{path} has truncated PBM payload")

    return width, height, payload


def pbm_to_png(input_path: Path, output_path: Path) -> None:
    width, height, payload = parse_pbm(input_path)
    row_bytes = (width + 7) // 8

    pixels = bytearray(width * height)
    for y in range(height):
        base = y * row_bytes
        for x in range(width):
            byte_val = payload[base + (x // 8)]
            bit = (byte_val >> (7 - (x % 8))) & 1
            # PBM bit 1 is black; output grayscale 0 (black), 255 (white).
            pixels[y * width + x] = 0 if bit else 255

    image = Image.frombytes("L", (width, height), bytes(pixels))
    image.save(output_path, format="PNG", optimize=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert PBM snapshots to PNG")
    parser.add_argument("input_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)

    pbm_files = sorted(args.input_dir.glob("*.pbm"))
    if not pbm_files:
        raise SystemExit(f"No PBM files found in {args.input_dir}")

    for pbm in pbm_files:
        out = args.output_dir / f"{pbm.stem}.png"
        pbm_to_png(pbm, out)
        print(f"converted {pbm} -> {out}")


if __name__ == "__main__":
    main()
