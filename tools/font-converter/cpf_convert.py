#!/usr/bin/env python3
"""
cpf_convert.py — CrossPoint Font (CPF) binary converter.

Rasterizes a TTF/OTF font file at a given pixel size using FreeType and writes
a .cpf binary file suitable for loading by SdFont on the CrossPoint Reader.

CPF binary format (22-byte header, then payload):
  Magic     4 bytes  "CPF\\x01"
  advanceY  1 byte   uint8    line height in pixels
  ascender  4 bytes  int32 LE
  descender 4 bytes  int32 LE
  is2Bit    1 byte   uint8    1 = 2-bit greyscale, 0 = 1-bit
  intervals 4 bytes  uint32 LE  number of unicode intervals
  glyphs    4 bytes  uint32 LE  total number of glyphs
  bitmapSz  4 bytes  uint32 LE  total bitmap bytes

Payload (in order):
  intervalCount * 12 bytes  EpdUnicodeInterval {first, last, offset} each uint32 LE
  totalGlyphs  * 16 bytes   EpdGlyph {w, h, advX, pad, left, top, dataLen, pad2, dataOff}
  bitmapSize   bytes         packed glyph bitmaps (2-bit or 1-bit, row-major, MSB-first)

Usage:
  python cpf_convert.py --font MyFont-Regular.ttf --size 14 --output MyFont-Regular.cpf
  python cpf_convert.py --font MyFont-Bold.ttf --size 14 --output MyFont-Bold.cpf [--1bit]

Dependencies:
  pip install freetype-py
"""

import argparse
import math
import struct
import sys
from collections import namedtuple
from pathlib import Path

try:
    import freetype
except ImportError:
    print("Error: freetype-py is not installed. Run: pip install freetype-py", file=sys.stderr)
    sys.exit(1)

# ---------------------------------------------------------------------------
# Unicode intervals to include (must be non-overlapping, ascending order).
# Matches the intervals used by the built-in fontconvert.py.
# ---------------------------------------------------------------------------
DEFAULT_INTERVALS = [
    (0x0000, 0x007F),  # Basic Latin
    (0x0080, 0x00FF),  # Latin-1 Supplement
    (0x0100, 0x017F),  # Latin Extended-A
    (0x0300, 0x036F),  # Combining Diacritical Marks
    (0x0400, 0x04FF),  # Cyrillic
    (0x2000, 0x206F),  # General Punctuation
    (0x2070, 0x209F),  # Superscripts and Subscripts
    (0x2190, 0x21FF),  # Arrows
    (0x2200, 0x22FF),  # Mathematical Operators
    (0x20A0, 0x20CF),  # Currency Symbols
    (0xFFFD, 0xFFFD),  # Replacement Character
]

GlyphProps = namedtuple(
    "GlyphProps", ["width", "height", "advance_x", "left", "top", "data_length", "data_offset"]
)


def norm_floor(val: int) -> int:
    return int(math.floor(val / (1 << 6)))


def norm_ceil(val: int) -> int:
    return int(math.ceil(val / (1 << 6)))


def rasterize_glyph_2bit(face: "freetype.Face") -> bytes:
    """Rasterize the current glyph as a 2-bit greyscale bitmap."""
    bitmap = face.glyph.bitmap
    w = bitmap.width
    rows = bitmap.rows
    buf = bitmap.buffer

    # Build 4-bit intermediate (2 pixels per byte, MSB first)
    pixels4g = []
    px = 0
    for i, v in enumerate(buf):
        x = i % w
        if x % 2 == 0:
            px = v >> 4
        else:
            px = px | (v & 0xF0)
            pixels4g.append(px)
            px = 0
        if x == w - 1 and w % 2 > 0:
            pixels4g.append(px)
            px = 0

    # Downsample to 2-bit (4 pixels per byte, MSB-first)
    pixels2b = []
    px = 0
    pitch = (w // 2) + (w % 2)
    for y in range(rows):
        for x in range(w):
            px = px << 2
            bm = pixels4g[y * pitch + (x // 2)]
            bm = (bm >> ((x % 2) * 4)) & 0xF
            if bm >= 12:
                px += 3
            elif bm >= 8:
                px += 2
            elif bm >= 4:
                px += 1
            if (y * w + x) % 4 == 3:
                pixels2b.append(px)
                px = 0
    if (w * rows) % 4 != 0:
        px = px << ((4 - (w * rows) % 4) * 2)
        pixels2b.append(px)

    return bytes(pixels2b)


def rasterize_glyph_1bit(face: "freetype.Face") -> bytes:
    """Rasterize the current glyph as a 1-bit black-and-white bitmap."""
    bitmap = face.glyph.bitmap
    w = bitmap.width
    rows = bitmap.rows
    buf = bitmap.buffer

    # Build 4-bit intermediate
    pixels4g = []
    px = 0
    for i, v in enumerate(buf):
        x = i % w
        if x % 2 == 0:
            px = v >> 4
        else:
            px = px | (v & 0xF0)
            pixels4g.append(px)
            px = 0
        if x == w - 1 and w % 2 > 0:
            pixels4g.append(px)
            px = 0

    # Downsample to 1-bit (8 pixels per byte, MSB-first; any shade >= 2 → black)
    pixelsbw = []
    px = 0
    pitch = (w // 2) + (w % 2)
    for y in range(rows):
        for x in range(w):
            px = px << 1
            bm = pixels4g[y * pitch + (x // 2)]
            px += 1 if ((x & 1) == 0 and bm & 0xE > 0) or ((x & 1) == 1 and bm & 0xE0 > 0) else 0
            if (y * w + x) % 8 == 7:
                pixelsbw.append(px)
                px = 0
    if (w * rows) % 8 != 0:
        px = px << (8 - (w * rows) % 8)
        pixelsbw.append(px)

    return bytes(pixelsbw)


def build_intervals(face: "freetype.Face", requested: list) -> list:
    """
    Return the subset of requested intervals where glyphs actually exist in
    the face.  Splits intervals around missing code points.
    """
    result = []
    for i_start, i_end in requested:
        start = i_start
        for cp in range(i_start, i_end + 1):
            if face.get_char_index(cp) == 0:
                if start < cp:
                    result.append((start, cp - 1))
                start = cp + 1
        if start <= i_end:
            result.append((start, i_end))
    return result


def convert(font_path: str, size: int, output_path: str, use_2bit: bool) -> None:
    face = freetype.Face(font_path)
    # 150 DPI — matches the existing fontconvert.py
    face.set_char_size(size << 6, size << 6, 150, 150)

    intervals = build_intervals(face, DEFAULT_INTERVALS)
    if not intervals:
        print("Error: no glyphs found for any configured interval.", file=sys.stderr)
        sys.exit(1)

    total_size = 0
    all_glyphs: list[tuple[GlyphProps, bytes]] = []

    for i_start, i_end in intervals:
        for cp in range(i_start, i_end + 1):
            idx = face.get_char_index(cp)
            if idx == 0:
                continue
            face.load_glyph(idx, freetype.FT_LOAD_RENDER)
            packed = rasterize_glyph_2bit(face) if use_2bit else rasterize_glyph_1bit(face)
            props = GlyphProps(
                width=face.glyph.bitmap.width,
                height=face.glyph.bitmap.rows,
                advance_x=norm_floor(face.glyph.advance.x),
                left=face.glyph.bitmap_left,
                top=face.glyph.bitmap_top,
                data_length=len(packed),
                data_offset=total_size,
            )
            total_size += len(packed)
            all_glyphs.append((props, packed))

    # Use '|' as proxy for the real line metrics (same heuristic as fontconvert.py)
    pipe_idx = face.get_char_index(ord("|"))
    if pipe_idx:
        face.load_glyph(pipe_idx, freetype.FT_LOAD_RENDER)

    advance_y = norm_ceil(face.size.height)
    ascender = norm_ceil(face.size.ascender)
    descender = norm_floor(face.size.descender)

    interval_count = len(intervals)
    total_glyphs = len(all_glyphs)
    bitmap_size = total_size

    # ------------------------------------------------------------------
    # Write CPF binary
    # ------------------------------------------------------------------
    # EpdGlyph layout (16 bytes, matches C struct with padding):
    #   uint8  width      +0
    #   uint8  height     +1
    #   uint8  advanceX   +2
    #   uint8  <pad>      +3
    #   int16  left       +4
    #   int16  top        +6
    #   uint16 dataLength +8
    #   uint16 <pad>      +10
    #   uint32 dataOffset +12
    GLYPH_FMT = "<BBBxhhHxxI"
    assert struct.calcsize(GLYPH_FMT) == 16, "EpdGlyph struct size mismatch"

    INTERVAL_FMT = "<III"
    assert struct.calcsize(INTERVAL_FMT) == 12, "EpdUnicodeInterval struct size mismatch"

    with open(output_path, "wb") as f:
        # Header (22 bytes)
        f.write(b"CPF\x01")
        f.write(struct.pack("<B", advance_y))
        f.write(struct.pack("<i", ascender))
        f.write(struct.pack("<i", descender))
        f.write(struct.pack("<B", 1 if use_2bit else 0))
        f.write(struct.pack("<I", interval_count))
        f.write(struct.pack("<I", total_glyphs))
        f.write(struct.pack("<I", bitmap_size))

        # Intervals
        glyph_offset = 0
        for i_start, i_end in intervals:
            f.write(struct.pack(INTERVAL_FMT, i_start, i_end, glyph_offset))
            glyph_offset += i_end - i_start + 1

        # Glyphs
        for props, _packed in all_glyphs:
            f.write(
                struct.pack(
                    GLYPH_FMT,
                    props.width,
                    props.height,
                    props.advance_x,
                    props.left,
                    props.top,
                    props.data_length,
                    props.data_offset,
                )
            )

        # Bitmaps
        for _props, packed in all_glyphs:
            f.write(packed)

    file_size = Path(output_path).stat().st_size
    mode_str = "2-bit" if use_2bit else "1-bit"
    print(
        f"Written: {output_path} ({file_size} bytes, {total_glyphs} glyphs, "
        f"{interval_count} intervals, {mode_str})"
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert TTF/OTF font to CrossPoint CPF binary format.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--font", required=True, help="Path to input TTF or OTF font file.")
    parser.add_argument("--size", type=int, required=True, help="Font size in pixels.")
    parser.add_argument("--output", required=True, help="Output .cpf file path.")
    parser.add_argument(
        "--1bit",
        dest="one_bit",
        action="store_true",
        help="Generate 1-bit B&W bitmap instead of 2-bit greyscale (smaller file, lower quality).",
    )
    args = parser.parse_args()

    use_2bit = not args.one_bit
    convert(args.font, args.size, args.output, use_2bit)


if __name__ == "__main__":
    main()
