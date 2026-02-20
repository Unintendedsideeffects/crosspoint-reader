#include "PngToBmpConverter.h"

#include <HardwareSerial.h>
#include <Logging.h>
#include <SdFat.h>
#include <miniz.h>

#include <cstdio>
#include <cstring>

#include "BitmapHelpers.h"

// ============================================================================
// IMAGE PROCESSING OPTIONS - Same settings as JpegToBmpConverter for consistency
// ============================================================================
constexpr bool USE_8BIT_OUTPUT = false;
constexpr bool USE_ATKINSON = true;
constexpr bool USE_FLOYD_STEINBERG = false;
constexpr int TARGET_MAX_WIDTH = 480;
constexpr int TARGET_MAX_HEIGHT = 800;
// ============================================================================

// PNG signature bytes
static constexpr uint8_t PNG_SIGNATURE[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

// PNG chunk types
static constexpr uint32_t CHUNK_IHDR = 0x49484452;  // 'IHDR'
static constexpr uint32_t CHUNK_IDAT = 0x49444154;  // 'IDAT'
static constexpr uint32_t CHUNK_IEND = 0x49454E44;  // 'IEND'
static constexpr uint32_t CHUNK_PLTE = 0x504C5445;  // 'PLTE'
static constexpr uint32_t CHUNK_tRNS = 0x74524E53;  // 'tRNS'

// PNG color types
static constexpr uint8_t PNG_COLOR_GRAYSCALE = 0;
static constexpr uint8_t PNG_COLOR_RGB = 2;
static constexpr uint8_t PNG_COLOR_INDEXED = 3;
static constexpr uint8_t PNG_COLOR_GRAYSCALE_ALPHA = 4;
static constexpr uint8_t PNG_COLOR_RGBA = 6;

// PNG filter types
static constexpr uint8_t PNG_FILTER_NONE = 0;
static constexpr uint8_t PNG_FILTER_SUB = 1;
static constexpr uint8_t PNG_FILTER_UP = 2;
static constexpr uint8_t PNG_FILTER_AVERAGE = 3;
static constexpr uint8_t PNG_FILTER_PAETH = 4;

// Context structure for PNG decoding
struct PngDecodeContext {
  FsFile& file;
  uint32_t width;
  uint32_t height;
  uint8_t bitDepth;
  uint8_t colorType;
  uint8_t bytesPerPixel;
  uint32_t rowBytes;  // Raw row bytes (without filter byte)

  // IDAT state
  uint32_t idatRemaining;  // Bytes left in current IDAT chunk
  bool foundFirstIdat;     // Have we found the first IDAT chunk?
  bool allIdatProcessed;   // Have we processed all IDAT chunks?

  // Inflate state
  tinfl_decompressor inflator;
  uint8_t* inflateInBuffer;  // Input buffer for compressed data
  size_t inflateInSize;      // Size of input buffer
  size_t inflateInPos;       // Current read position in input buffer
  size_t inflateInFilled;    // Amount of data currently in input buffer

  uint8_t* inflateOutBuffer;  // Output buffer (dictionary)
  size_t inflateOutPos;       // Current position in output buffer

  // Row state
  uint8_t* prevRowBuffer;  // Previous row for unfiltering
  uint8_t* currRowBuffer;  // Current row being built
  size_t currRowPos;       // Position in current row (including filter byte)

  // Palette for indexed color
  uint8_t palette[256 * 3];   // RGB palette
  uint8_t paletteAlpha[256];  // Alpha values for palette
  uint16_t paletteCount;
  bool hasPaletteAlpha;
};

inline bool readBigEndian32(FsFile& f, uint32_t& value) {
  uint8_t buf[4];
  const size_t read = f.read(buf, 4);
  if (read != 4) {
    return false;
  }
  value = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
          (static_cast<uint32_t>(buf[2]) << 8) | buf[3];
  return true;
}

inline void write16(Print& out, const uint16_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
}

inline void write32(Print& out, const uint32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

inline void write32Signed(Print& out, const int32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

// Helper function: Write BMP header with 8-bit grayscale (256 levels)
static void writeBmpHeader8bit(Print& bmpOut, const int width, const int height) {
  const int bytesPerRow = (width + 3) / 4 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t paletteSize = 256 * 4;
  const uint32_t fileSize = 14 + 40 + paletteSize + imageSize;

  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);
  write32(bmpOut, 0);
  write32(bmpOut, 14 + 40 + paletteSize);

  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);
  write16(bmpOut, 1);
  write16(bmpOut, 8);
  write32(bmpOut, 0);
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);
  write32(bmpOut, 2835);
  write32(bmpOut, 256);
  write32(bmpOut, 256);

  for (int i = 0; i < 256; i++) {
    bmpOut.write(static_cast<uint8_t>(i));
    bmpOut.write(static_cast<uint8_t>(i));
    bmpOut.write(static_cast<uint8_t>(i));
    bmpOut.write(static_cast<uint8_t>(0));
  }
}

static void writeBmpHeader1bit(Print& bmpOut, const int width, const int height) {
  const int bytesPerRow = (width + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 62 + imageSize;

  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);
  write32(bmpOut, 0);
  write32(bmpOut, 62);

  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);
  write16(bmpOut, 1);
  write16(bmpOut, 1);
  write32(bmpOut, 0);
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);
  write32(bmpOut, 2835);
  write32(bmpOut, 2);
  write32(bmpOut, 2);

  uint8_t palette[8] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  for (const uint8_t i : palette) {
    bmpOut.write(i);
  }
}

static void writeBmpHeader2bit(Print& bmpOut, const int width, const int height) {
  const int bytesPerRow = (width * 2 + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 70 + imageSize;

  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);
  write32(bmpOut, 0);
  write32(bmpOut, 70);

  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);
  write16(bmpOut, 1);
  write16(bmpOut, 2);
  write32(bmpOut, 0);
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);
  write32(bmpOut, 2835);
  write32(bmpOut, 4);
  write32(bmpOut, 4);

  uint8_t palette[16] = {0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55, 0x00,
                         0xAA, 0xAA, 0xAA, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  for (const uint8_t i : palette) {
    bmpOut.write(i);
  }
}

// Paeth predictor function
static inline uint8_t paethPredictor(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = abs(p - a);
  const int pb = abs(p - b);
  const int pc = abs(p - c);
  if (pa <= pb && pa <= pc)
    return static_cast<uint8_t>(a);
  else if (pb <= pc)
    return static_cast<uint8_t>(b);
  else
    return static_cast<uint8_t>(c);
}

// Apply PNG unfiltering to a row
static void unfilterRow(uint8_t filterType, uint8_t* curr, const uint8_t* prev, int bpp, int rowBytes) {
  switch (filterType) {
    case PNG_FILTER_NONE:
      // No filtering
      break;

    case PNG_FILTER_SUB:
      // Sub: curr[i] += curr[i - bpp]
      for (int i = bpp; i < rowBytes; i++) {
        curr[i] = static_cast<uint8_t>(curr[i] + curr[i - bpp]);
      }
      break;

    case PNG_FILTER_UP:
      // Up: curr[i] += prev[i]
      for (int i = 0; i < rowBytes; i++) {
        curr[i] = static_cast<uint8_t>(curr[i] + prev[i]);
      }
      break;

    case PNG_FILTER_AVERAGE:
      // Average: curr[i] += floor((curr[i-bpp] + prev[i]) / 2)
      for (int i = 0; i < rowBytes; i++) {
        int a = (i >= bpp) ? curr[i - bpp] : 0;
        int b = prev[i];
        curr[i] = static_cast<uint8_t>(curr[i] + ((a + b) >> 1));
      }
      break;

    case PNG_FILTER_PAETH:
      // Paeth: curr[i] += paeth(curr[i-bpp], prev[i], prev[i-bpp])
      for (int i = 0; i < rowBytes; i++) {
        int a = (i >= bpp) ? curr[i - bpp] : 0;
        int b = prev[i];
        int c = (i >= bpp) ? prev[i - bpp] : 0;
        curr[i] = static_cast<uint8_t>(curr[i] + paethPredictor(a, b, c));
      }
      break;

    default:
      LOG_ERR("PNG", "Unknown filter type: %d", filterType);
      break;
  }
}

// Convert a raw pixel to grayscale based on color type
static inline uint8_t pixelToGrayscale(const uint8_t* pixel, uint8_t colorType, uint8_t bitDepth,
                                       const PngDecodeContext& ctx) {
  switch (colorType) {
    case PNG_COLOR_GRAYSCALE:
      if (bitDepth == 8) {
        return pixel[0];
      } else if (bitDepth == 16) {
        return pixel[0];  // Use high byte
      } else if (bitDepth < 8) {
        // Scale up to 8 bits
        return static_cast<uint8_t>(pixel[0] * 255 / ((1 << bitDepth) - 1));
      }
      return pixel[0];

    case PNG_COLOR_GRAYSCALE_ALPHA:
      // Just use the grayscale value (ignore alpha for e-ink)
      return pixel[0];

    case PNG_COLOR_RGB:
      return static_cast<uint8_t>((pixel[0] * 25 + pixel[1] * 50 + pixel[2] * 25) / 100);

    case PNG_COLOR_RGBA:
      // Convert RGB to grayscale (ignore alpha for e-ink display)
      return static_cast<uint8_t>((pixel[0] * 25 + pixel[1] * 50 + pixel[2] * 25) / 100);

    case PNG_COLOR_INDEXED: {
      // Look up palette color
      uint8_t idx = pixel[0];
      if (idx < ctx.paletteCount) {
        uint8_t r = ctx.palette[idx * 3];
        uint8_t g = ctx.palette[idx * 3 + 1];
        uint8_t b = ctx.palette[idx * 3 + 2];
        return static_cast<uint8_t>((r * 25 + g * 50 + b * 25) / 100);
      }
      return 0;
    }

    default:
      return 128;
  }
}

// Read more compressed data from IDAT chunks
static bool refillInflateBuffer(PngDecodeContext& ctx) {
  // If we've processed all IDAT chunks, nothing more to read
  if (ctx.allIdatProcessed) {
    return ctx.inflateInFilled > ctx.inflateInPos;
  }

  // Move remaining data to start of buffer
  if (ctx.inflateInPos > 0 && ctx.inflateInFilled > ctx.inflateInPos) {
    memmove(ctx.inflateInBuffer, ctx.inflateInBuffer + ctx.inflateInPos, ctx.inflateInFilled - ctx.inflateInPos);
    ctx.inflateInFilled -= ctx.inflateInPos;
    ctx.inflateInPos = 0;
  } else {
    ctx.inflateInFilled = 0;
    ctx.inflateInPos = 0;
  }

  // Fill buffer from IDAT chunks
  while (ctx.inflateInFilled < ctx.inflateInSize) {
    // If current IDAT is exhausted, find next one
    if (ctx.idatRemaining == 0) {
      // Skip CRC of previous chunk
      if (ctx.foundFirstIdat) {
        ctx.file.seekCur(4);  // Skip CRC
      }

      // Look for next IDAT chunk
      while (true) {
        uint32_t chunkLen = 0;
        uint32_t chunkType = 0;
        if (!readBigEndian32(ctx.file, chunkLen) || !readBigEndian32(ctx.file, chunkType)) {
          ctx.allIdatProcessed = true;
          return ctx.inflateInFilled > 0;
        }

        if (chunkType == CHUNK_IDAT) {
          ctx.idatRemaining = chunkLen;
          ctx.foundFirstIdat = true;
          break;
        } else if (chunkType == CHUNK_IEND) {
          ctx.allIdatProcessed = true;
          return ctx.inflateInFilled > 0;
        } else {
          // Skip unknown chunk
          ctx.file.seekCur(chunkLen + 4);  // data + CRC
        }

        if (!ctx.file.available()) {
          ctx.allIdatProcessed = true;
          return ctx.inflateInFilled > 0;
        }
      }
    }

    // Read from current IDAT
    size_t toRead = ctx.inflateInSize - ctx.inflateInFilled;
    if (toRead > ctx.idatRemaining) {
      toRead = ctx.idatRemaining;
    }

    size_t bytesRead = ctx.file.read(ctx.inflateInBuffer + ctx.inflateInFilled, toRead);
    ctx.inflateInFilled += bytesRead;
    ctx.idatRemaining -= bytesRead;

    if (bytesRead == 0 && ctx.idatRemaining > 0) {
      LOG_ERR("PNG", "Unexpected EOF in IDAT");
      return false;
    }
  }

  return true;
}

// Decompress more data from the inflate stream
static bool decompressMore(PngDecodeContext& ctx, uint8_t* outBuf, size_t* outLen) {
  // Refill input if needed
  if (ctx.inflateInPos >= ctx.inflateInFilled) {
    if (!refillInflateBuffer(ctx)) {
      *outLen = 0;
      return true;  // No more data available
    }
  }

  size_t inBytes = ctx.inflateInFilled - ctx.inflateInPos;
  size_t outBytes = *outLen;

  int flags = 0;
  if (!ctx.allIdatProcessed || ctx.idatRemaining > 0 || ctx.inflateInFilled > ctx.inflateInPos) {
    flags = TINFL_FLAG_HAS_MORE_INPUT;
  }

  tinfl_status status = tinfl_decompress(&ctx.inflator, ctx.inflateInBuffer + ctx.inflateInPos, &inBytes, outBuf,
                                         outBuf, &outBytes, flags | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

  ctx.inflateInPos += inBytes;
  *outLen = outBytes;

  if (status < 0) {
    LOG_ERR("PNG", "Inflate error: %d", status);
    return false;
  }

  return true;
}

// Parse PNG headers and set up decode context
static bool parsePngHeaders(PngDecodeContext& ctx) {
  // Check PNG signature
  uint8_t sig[8];
  if (ctx.file.read(sig, 8) != 8) {
    LOG_ERR("PNG", "Failed to read PNG signature");
    return false;
  }

  if (memcmp(sig, PNG_SIGNATURE, 8) != 0) {
    LOG_ERR("PNG", "Invalid PNG signature");
    return false;
  }

  // Read IHDR chunk
  uint32_t chunkLen = 0;
  uint32_t chunkType = 0;
  if (!readBigEndian32(ctx.file, chunkLen) || !readBigEndian32(ctx.file, chunkType)) {
    LOG_ERR("PNG", "Failed to read IHDR chunk header");
    return false;
  }

  if (chunkType != CHUNK_IHDR || chunkLen != 13) {
    LOG_ERR("PNG", "Expected IHDR chunk");
    return false;
  }

  if (!readBigEndian32(ctx.file, ctx.width) || !readBigEndian32(ctx.file, ctx.height)) {
    LOG_ERR("PNG", "Failed to read IHDR dimensions");
    return false;
  }
  ctx.bitDepth = ctx.file.read();
  ctx.colorType = ctx.file.read();
  uint8_t compression = ctx.file.read();
  uint8_t filter = ctx.file.read();
  uint8_t interlace = ctx.file.read();

  // Skip CRC
  ctx.file.seekCur(4);

  LOG_DBG("PNG", "Image: %dx%d, depth=%d, colorType=%d, interlace=%d", ctx.width, ctx.height, ctx.bitDepth,
          ctx.colorType, interlace);

  // Validate
  if (compression != 0) {
    LOG_ERR("PNG", "Unsupported compression method");
    return false;
  }

  if (filter != 0) {
    LOG_ERR("PNG", "Unsupported filter method");
    return false;
  }

  if (interlace != 0) {
    LOG_ERR("PNG", "Interlaced PNGs not supported");
    return false;
  }

  // Calculate bytes per pixel and row bytes
  switch (ctx.colorType) {
    case PNG_COLOR_GRAYSCALE:
      ctx.bytesPerPixel = (ctx.bitDepth + 7) / 8;
      break;
    case PNG_COLOR_RGB:
      ctx.bytesPerPixel = 3 * ((ctx.bitDepth + 7) / 8);
      break;
    case PNG_COLOR_INDEXED:
      ctx.bytesPerPixel = 1;
      break;
    case PNG_COLOR_GRAYSCALE_ALPHA:
      ctx.bytesPerPixel = 2 * ((ctx.bitDepth + 7) / 8);
      break;
    case PNG_COLOR_RGBA:
      ctx.bytesPerPixel = 4 * ((ctx.bitDepth + 7) / 8);
      break;
    default:
      LOG_ERR("PNG", "Unsupported color type: %d", ctx.colorType);
      return false;
  }

  // Calculate row bytes (bits per row rounded up to bytes)
  if (ctx.bitDepth < 8) {
    ctx.rowBytes = (ctx.width * ctx.bitDepth + 7) / 8;
  } else {
    ctx.rowBytes = ctx.width * ctx.bytesPerPixel;
  }

  // Process chunks until first IDAT, looking for PLTE
  ctx.paletteCount = 0;
  ctx.hasPaletteAlpha = false;
  memset(ctx.paletteAlpha, 255, sizeof(ctx.paletteAlpha));

  while (true) {
    if (!readBigEndian32(ctx.file, chunkLen) || !readBigEndian32(ctx.file, chunkType)) {
      LOG_ERR("PNG", "Failed to read chunk header");
      return false;
    }

    if (chunkType == CHUNK_IDAT) {
      // Found first IDAT - rewind so main loop can process it
      ctx.file.seekCur(-8);
      break;
    } else if (chunkType == CHUNK_PLTE) {
      // Read palette
      ctx.paletteCount = chunkLen / 3;
      if (ctx.paletteCount > 256) ctx.paletteCount = 256;
      const size_t paletteBytes = static_cast<size_t>(ctx.paletteCount) * 3;
      if (ctx.file.read(ctx.palette, paletteBytes) != paletteBytes) {
        LOG_ERR("PNG", "Truncated PLTE chunk");
        return false;
      }
      // Skip any remaining palette data beyond 256 entries, plus CRC
      ctx.file.seekCur(static_cast<int32_t>(chunkLen - paletteBytes) + 4);
    } else if (chunkType == CHUNK_tRNS) {
      // Transparency chunk
      if (ctx.colorType == PNG_COLOR_INDEXED) {
        ctx.hasPaletteAlpha = true;
        size_t alphaCount = chunkLen < 256 ? chunkLen : 256;
        if (ctx.file.read(ctx.paletteAlpha, alphaCount) != alphaCount) {
          LOG_ERR("PNG", "Truncated tRNS chunk");
          return false;
        }
        ctx.file.seekCur(chunkLen - alphaCount + 4);  // Skip rest + CRC
      } else {
        ctx.file.seekCur(chunkLen + 4);
      }
    } else if (chunkType == CHUNK_IEND) {
      LOG_ERR("PNG", "No IDAT chunk found");
      return false;
    } else {
      // Skip unknown chunk
      ctx.file.seekCur(chunkLen + 4);
    }

    if (!ctx.file.available()) {
      LOG_ERR("PNG", "Unexpected end of file");
      return false;
    }
  }

  // Require palette for indexed images
  if (ctx.colorType == PNG_COLOR_INDEXED && ctx.paletteCount == 0) {
    LOG_ERR("PNG", "Indexed PNG requires PLTE chunk");
    return false;
  }

  return true;
}

bool PngToBmpConverter::pngFileToBmpStreamInternal(FsFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight,
                                                   bool oneBit, bool crop) {
  LOG_DBG("PNG", "Converting PNG to %s BMP (target: %dx%d)", oneBit ? "1-bit" : "2-bit", targetWidth, targetHeight);

  // Initialize context
  PngDecodeContext ctx = {.file = pngFile};
  ctx.idatRemaining = 0;
  ctx.foundFirstIdat = false;
  ctx.allIdatProcessed = false;
  ctx.inflateInPos = 0;
  ctx.inflateInFilled = 0;
  ctx.inflateOutPos = 0;
  ctx.currRowPos = 0;

  // Parse headers
  if (!parsePngHeaders(ctx)) {
    return false;
  }

  // Safety limits
  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;

  if (ctx.width > MAX_IMAGE_WIDTH || ctx.height > MAX_IMAGE_HEIGHT) {
    LOG_ERR("PNG", "Image too large (%dx%d), max supported: %dx%d", ctx.width, ctx.height, MAX_IMAGE_WIDTH,
            MAX_IMAGE_HEIGHT);
    return false;
  }

  // Calculate output dimensions
  int outWidth = ctx.width;
  int outHeight = ctx.height;
  uint32_t scaleX_fp = 65536;
  uint32_t scaleY_fp = 65536;
  bool needsScaling = false;

  if (targetWidth > 0 && targetHeight > 0 &&
      (static_cast<int>(ctx.width) > targetWidth || static_cast<int>(ctx.height) > targetHeight)) {
    const float scaleToFitWidth = static_cast<float>(targetWidth) / ctx.width;
    const float scaleToFitHeight = static_cast<float>(targetHeight) / ctx.height;

    float scale;
    if (crop) {
      scale = (scaleToFitWidth > scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;
    } else {
      scale = (scaleToFitWidth < scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;
    }

    outWidth = static_cast<int>(ctx.width * scale);
    outHeight = static_cast<int>(ctx.height * scale);

    if (outWidth < 1) outWidth = 1;
    if (outHeight < 1) outHeight = 1;

    scaleX_fp = (static_cast<uint32_t>(ctx.width) << 16) / outWidth;
    scaleY_fp = (static_cast<uint32_t>(ctx.height) << 16) / outHeight;
    needsScaling = true;

    LOG_DBG("PNG", "Pre-scaling %dx%d -> %dx%d", ctx.width, ctx.height, outWidth, outHeight);
  }

  // Allocate buffers
  ctx.inflateInSize = 1024;
  ctx.inflateInBuffer = static_cast<uint8_t*>(malloc(ctx.inflateInSize));
  if (!ctx.inflateInBuffer) {
    LOG_ERR("PNG", "Failed to allocate inflate input buffer");
    return false;
  }

  // Row buffers (+1 for filter byte)
  ctx.prevRowBuffer = static_cast<uint8_t*>(malloc(ctx.rowBytes + 1));
  ctx.currRowBuffer = static_cast<uint8_t*>(malloc(ctx.rowBytes + 1));
  if (!ctx.prevRowBuffer || !ctx.currRowBuffer) {
    LOG_ERR("PNG", "Failed to allocate row buffers");
    free(ctx.inflateInBuffer);
    if (ctx.prevRowBuffer) free(ctx.prevRowBuffer);
    if (ctx.currRowBuffer) free(ctx.currRowBuffer);
    return false;
  }
  memset(ctx.prevRowBuffer, 0, ctx.rowBytes + 1);
  memset(ctx.currRowBuffer, 0, ctx.rowBytes + 1);

  // Initialize inflator
  tinfl_init(&ctx.inflator);

  // Write BMP header
  int bytesPerRow;
  if (USE_8BIT_OUTPUT && !oneBit) {
    writeBmpHeader8bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth + 3) / 4 * 4;
  } else if (oneBit) {
    writeBmpHeader1bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth + 31) / 32 * 4;
  } else {
    writeBmpHeader2bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth * 2 + 31) / 32 * 4;
  }

  // BMP row output buffer
  auto* rowBuffer = static_cast<uint8_t*>(malloc(bytesPerRow));
  if (!rowBuffer) {
    LOG_ERR("PNG", "Failed to allocate BMP row buffer");
    free(ctx.inflateInBuffer);
    free(ctx.prevRowBuffer);
    free(ctx.currRowBuffer);
    return false;
  }

  // Grayscale row buffer (one full row of grayscale pixels)
  auto* grayRow = static_cast<uint8_t*>(malloc(ctx.width));
  if (!grayRow) {
    LOG_ERR("PNG", "Failed to allocate grayscale row buffer");
    free(ctx.inflateInBuffer);
    free(ctx.prevRowBuffer);
    free(ctx.currRowBuffer);
    free(rowBuffer);
    return false;
  }

  // Create ditherers
  AtkinsonDitherer* atkinsonDitherer = nullptr;
  FloydSteinbergDitherer* fsDitherer = nullptr;
  Atkinson1BitDitherer* atkinson1BitDitherer = nullptr;

  if (oneBit) {
    atkinson1BitDitherer = new Atkinson1BitDitherer(outWidth);
  } else if (!USE_8BIT_OUTPUT) {
    if (USE_ATKINSON) {
      atkinsonDitherer = new AtkinsonDitherer(outWidth);
    } else if (USE_FLOYD_STEINBERG) {
      fsDitherer = new FloydSteinbergDitherer(outWidth);
    }
  }

  // Scaling accumulators
  uint32_t* rowAccum = nullptr;
  uint16_t* rowCount = nullptr;
  int currentOutY = 0;
  uint32_t nextOutY_srcStart = 0;

  if (needsScaling) {
    rowAccum = new uint32_t[outWidth]();
    rowCount = new uint16_t[outWidth]();
    nextOutY_srcStart = scaleY_fp;
  }

  // Process rows
  uint8_t filterByte = 0;
  bool gotFilterByte = false;
  bool success = true;

  for (uint32_t y = 0; y < ctx.height && success; y++) {
    // Decompress one row worth of data
    ctx.currRowPos = 0;

    while (ctx.currRowPos <= ctx.rowBytes) {
      size_t needed = ctx.rowBytes + 1 - ctx.currRowPos;
      size_t got = needed;

      if (!decompressMore(ctx, ctx.currRowBuffer + ctx.currRowPos, &got)) {
        LOG_ERR("PNG", "Decompression failed at row %d", y);
        success = false;
        break;
      }

      ctx.currRowPos += got;

      if (got == 0) {
        // Need more input
        if (!refillInflateBuffer(ctx)) {
          if (ctx.currRowPos < ctx.rowBytes + 1) {
            LOG_ERR("PNG", "Unexpected end of compressed data at row %d", y);
            success = false;
          }
          break;
        }
      }
    }

    if (!success) break;

    // Extract filter byte and apply unfiltering
    filterByte = ctx.currRowBuffer[0];
    unfilterRow(filterByte, ctx.currRowBuffer + 1, ctx.prevRowBuffer + 1, ctx.bytesPerPixel, ctx.rowBytes);

    // Convert row to grayscale
    const uint8_t* rowData = ctx.currRowBuffer + 1;

    if (ctx.bitDepth < 8) {
      // Handle sub-byte pixels (1, 2, 4 bit)
      int pixelsPerByte = 8 / ctx.bitDepth;
      uint8_t mask = (1 << ctx.bitDepth) - 1;
      for (uint32_t x = 0; x < ctx.width; x++) {
        int byteIdx = x / pixelsPerByte;
        int bitIdx = (pixelsPerByte - 1 - (x % pixelsPerByte)) * ctx.bitDepth;
        uint8_t pixel = (rowData[byteIdx] >> bitIdx) & mask;
        // Scale to 8-bit
        grayRow[x] = pixelToGrayscale(&pixel, ctx.colorType, ctx.bitDepth, ctx);
      }
    } else {
      // 8 or 16 bit pixels
      for (uint32_t x = 0; x < ctx.width; x++) {
        grayRow[x] = pixelToGrayscale(rowData + x * ctx.bytesPerPixel, ctx.colorType, ctx.bitDepth, ctx);
      }
    }

    // Swap row buffers
    uint8_t* tmp = ctx.prevRowBuffer;
    ctx.prevRowBuffer = ctx.currRowBuffer;
    ctx.currRowBuffer = tmp;

    // Output row (with optional scaling)
    if (!needsScaling) {
      memset(rowBuffer, 0, bytesPerRow);

      if (USE_8BIT_OUTPUT && !oneBit) {
        for (int x = 0; x < outWidth; x++) {
          rowBuffer[x] = adjustPixel(grayRow[x]);
        }
      } else if (oneBit) {
        for (int x = 0; x < outWidth; x++) {
          const uint8_t bit =
              atkinson1BitDitherer ? atkinson1BitDitherer->processPixel(grayRow[x], x) : quantize1bit(grayRow[x], x, y);
          const int byteIndex = x / 8;
          const int bitOffset = 7 - (x % 8);
          rowBuffer[byteIndex] |= (bit << bitOffset);
        }
        if (atkinson1BitDitherer) atkinson1BitDitherer->nextRow();
      } else {
        for (int x = 0; x < outWidth; x++) {
          const uint8_t gray = adjustPixel(grayRow[x]);
          uint8_t twoBit;
          if (atkinsonDitherer) {
            twoBit = atkinsonDitherer->processPixel(gray, x);
          } else if (fsDitherer) {
            twoBit = fsDitherer->processPixel(gray, x);
          } else {
            twoBit = quantize(gray, x, y);
          }
          const int byteIndex = (x * 2) / 8;
          const int bitOffset = 6 - ((x * 2) % 8);
          rowBuffer[byteIndex] |= (twoBit << bitOffset);
        }
        if (atkinsonDitherer)
          atkinsonDitherer->nextRow();
        else if (fsDitherer)
          fsDitherer->nextRow();
      }
      bmpOut.write(rowBuffer, bytesPerRow);
    } else {
      // Scaling: accumulate into output rows
      for (int outX = 0; outX < outWidth; outX++) {
        const int srcXStart = (static_cast<uint32_t>(outX) * scaleX_fp) >> 16;
        const int srcXEnd = (static_cast<uint32_t>(outX + 1) * scaleX_fp) >> 16;

        int sum = 0;
        int count = 0;
        for (int srcX = srcXStart; srcX < srcXEnd && srcX < static_cast<int>(ctx.width); srcX++) {
          sum += grayRow[srcX];
          count++;
        }

        if (count == 0 && srcXStart < static_cast<int>(ctx.width)) {
          sum = grayRow[srcXStart];
          count = 1;
        }

        rowAccum[outX] += sum;
        rowCount[outX] += count;
      }

      // Check if we've completed an output row
      const uint32_t srcY_fp = static_cast<uint32_t>(y + 1) << 16;

      if (srcY_fp >= nextOutY_srcStart && currentOutY < outHeight) {
        memset(rowBuffer, 0, bytesPerRow);

        if (USE_8BIT_OUTPUT && !oneBit) {
          for (int x = 0; x < outWidth; x++) {
            const uint8_t gray = (rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 0;
            rowBuffer[x] = adjustPixel(gray);
          }
        } else if (oneBit) {
          for (int x = 0; x < outWidth; x++) {
            const uint8_t gray = (rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 0;
            const uint8_t bit =
                atkinson1BitDitherer ? atkinson1BitDitherer->processPixel(gray, x) : quantize1bit(gray, x, currentOutY);
            const int byteIndex = x / 8;
            const int bitOffset = 7 - (x % 8);
            rowBuffer[byteIndex] |= (bit << bitOffset);
          }
          if (atkinson1BitDitherer) atkinson1BitDitherer->nextRow();
        } else {
          for (int x = 0; x < outWidth; x++) {
            const uint8_t gray = adjustPixel((rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 0);
            uint8_t twoBit;
            if (atkinsonDitherer) {
              twoBit = atkinsonDitherer->processPixel(gray, x);
            } else if (fsDitherer) {
              twoBit = fsDitherer->processPixel(gray, x);
            } else {
              twoBit = quantize(gray, x, currentOutY);
            }
            const int byteIndex = (x * 2) / 8;
            const int bitOffset = 6 - ((x * 2) % 8);
            rowBuffer[byteIndex] |= (twoBit << bitOffset);
          }
          if (atkinsonDitherer)
            atkinsonDitherer->nextRow();
          else if (fsDitherer)
            fsDitherer->nextRow();
        }

        bmpOut.write(rowBuffer, bytesPerRow);
        currentOutY++;

        memset(rowAccum, 0, outWidth * sizeof(uint32_t));
        memset(rowCount, 0, outWidth * sizeof(uint16_t));
        nextOutY_srcStart = static_cast<uint32_t>(currentOutY + 1) * scaleY_fp;
      }
    }
  }

  // Cleanup
  if (rowAccum) delete[] rowAccum;
  if (rowCount) delete[] rowCount;
  if (atkinsonDitherer) delete atkinsonDitherer;
  if (fsDitherer) delete fsDitherer;
  if (atkinson1BitDitherer) delete atkinson1BitDitherer;
  free(grayRow);
  free(rowBuffer);
  free(ctx.inflateInBuffer);
  free(ctx.prevRowBuffer);
  free(ctx.currRowBuffer);

  if (success) {
    LOG_DBG("PNG", "Successfully converted PNG to BMP");
  }
  return success;
}

bool PngToBmpConverter::pngFileToBmpStream(FsFile& pngFile, Print& bmpOut, bool crop) {
  return pngFileToBmpStreamInternal(pngFile, bmpOut, TARGET_MAX_WIDTH, TARGET_MAX_HEIGHT, false, crop);
}

bool PngToBmpConverter::pngFileToBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth,
                                                   int targetMaxHeight, bool crop) {
  return pngFileToBmpStreamInternal(pngFile, bmpOut, targetMaxWidth, targetMaxHeight, false, crop);
}

bool PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth,
                                                       int targetMaxHeight, bool crop) {
  return pngFileToBmpStreamInternal(pngFile, bmpOut, targetMaxWidth, targetMaxHeight, true, crop);
}
