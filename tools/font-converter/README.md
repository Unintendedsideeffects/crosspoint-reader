# CrossPoint Font Converter

Convert TTF/OTF fonts to the CPF binary format used by the CrossPoint Reader's
`SdFont` loader.

## Requirements

```bash
pip install freetype-py
```

## Usage

```bash
# Convert a regular weight font at size 14 (2-bit greyscale, recommended)
python cpf_convert.py --font MyFont-Regular.ttf --size 14 --output MyFont-Regular.cpf

# Convert a bold weight
python cpf_convert.py --font MyFont-Bold.ttf --size 14 --output MyFont-Bold.cpf

# Convert italic and bold-italic if needed
python cpf_convert.py --font MyFont-Italic.ttf --size 14 --output MyFont-Italic.cpf
python cpf_convert.py --font MyFont-BoldItalic.ttf --size 14 --output MyFont-BoldItalic.cpf

# 1-bit mode (smaller file, no greyscale anti-aliasing)
python cpf_convert.py --font MyFont-Regular.ttf --size 14 --output MyFont-Regular.cpf --1bit
```

## Installing on the device

1. Copy the `.cpf` files to the `/fonts/` directory on the SD card.
   - Via the web UI: Settings page → **Install Font (.cpf)** card → upload.
   - Via USB mass storage: copy directly to the `fonts/` folder on the SD card.
2. In the device settings, set **Font Family** to **External Font**.
3. In the device settings, set **External Font** to the new font family.

## Naming convention

The CrossPoint `UserFontManager` detects font variants by filename suffix.
For a font family named `MyFont`, use:

| File                    | Variant      |
|-------------------------|--------------|
| `MyFont-Regular.cpf`    | Regular      |
| `MyFont-Bold.cpf`       | Bold         |
| `MyFont-Italic.cpf`     | Italic       |
| `MyFont-BoldItalic.cpf` | Bold Italic  |

Check `UserFontManager.cpp` for the exact suffix matching rules.

## CPF file format

```
Header (22 bytes):
  "CPF\x01"       4 bytes  magic
  advanceY        1 byte   uint8   line height (pixels)
  ascender        4 bytes  int32   LE
  descender       4 bytes  int32   LE
  is2Bit          1 byte   uint8   1=2-bit, 0=1-bit
  intervalCount   4 bytes  uint32  LE
  totalGlyphs     4 bytes  uint32  LE
  bitmapSize      4 bytes  uint32  LE

Payload:
  intervalCount × EpdUnicodeInterval (12 bytes each):
    first   uint32 LE
    last    uint32 LE
    offset  uint32 LE  (index into glyph array)

  totalGlyphs × EpdGlyph (16 bytes each):
    width       uint8
    height      uint8
    advanceX    uint8
    <pad>       1 byte
    left        int16  LE
    top         int16  LE
    dataLength  uint16 LE
    <pad>       2 bytes
    dataOffset  uint32 LE  (byte offset into bitmap blob)

  bitmapSize bytes of packed glyph bitmaps:
    2-bit: 4 pixels/byte, MSB-first, row-major
    1-bit: 8 pixels/byte, MSB-first, row-major
```
