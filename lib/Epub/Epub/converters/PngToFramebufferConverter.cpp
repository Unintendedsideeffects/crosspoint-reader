#include "PngToFramebufferConverter.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <Logging.h>
#include <PNGdec.h>
#include <SDCardManager.h>
#include <SdFat.h>

#include <cstdlib>
#include <new>

#include "DitherUtils.h"
#include "PixelCache.h"

namespace {

// Context struct passed through PNGdec callbacks to avoid global mutable state.
struct PngContext {
  GfxRenderer* renderer;
  const RenderConfig* config;
  int screenWidth;
  int screenHeight;

  float scale;
  int srcWidth;
  int srcHeight;
  int dstWidth;
  int dstHeight;
  int lastDstY;

  PixelCache cache;
  bool caching;

  uint8_t* grayLineBuffer;

  PngContext()
      : renderer(nullptr),
        config(nullptr),
        screenWidth(0),
        screenHeight(0),
        scale(1.0f),
        srcWidth(0),
        srcHeight(0),
        dstWidth(0),
        dstHeight(0),
        lastDstY(-1),
        caching(false),
        grayLineBuffer(nullptr) {}
};

void* pngOpenWithHandle(const char* filename, int32_t* size) {
  FsFile* f = new FsFile();
  if (!Storage.openFileForRead("PNG", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}

void pngCloseWithHandle(void* handle) {
  FsFile* f = reinterpret_cast<FsFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

int32_t pngReadWithHandle(PNGFILE* pFile, uint8_t* pBuf, int32_t len) {
  FsFile* f = reinterpret_cast<FsFile*>(pFile->fHandle);
  if (!f) return 0;
  return f->read(pBuf, len);
}

int32_t pngSeekWithHandle(PNGFILE* pFile, int32_t pos) {
  FsFile* f = reinterpret_cast<FsFile*>(pFile->fHandle);
  if (!f) return -1;
  return f->seek(pos);
}

constexpr size_t PNG_DECODER_APPROX_SIZE = 44 * 1024;
constexpr size_t MIN_FREE_HEAP_FOR_PNG = PNG_DECODER_APPROX_SIZE + 16 * 1024;

int bytesPerPixelFromType(int pixelType) {
  switch (pixelType) {
    case PNG_PIXEL_TRUECOLOR:
      return 3;
    case PNG_PIXEL_GRAY_ALPHA:
      return 2;
    case PNG_PIXEL_TRUECOLOR_ALPHA:
      return 4;
    case PNG_PIXEL_GRAYSCALE:
    case PNG_PIXEL_INDEXED:
    default:
      return 1;
  }
}

int requiredPngInternalBufferBytes(int srcWidth, int pixelType) {
  int pitch = srcWidth * bytesPerPixelFromType(pixelType);
  return ((pitch + 1) * 2) + 32;
}

void convertLineToGray(uint8_t* pPixels, uint8_t* grayLine, int width, int pixelType, uint8_t* palette, int hasAlpha) {
  switch (pixelType) {
    case PNG_PIXEL_GRAYSCALE:
      memcpy(grayLine, pPixels, width);
      break;
    case PNG_PIXEL_TRUECOLOR:
      for (int x = 0; x < width; x++) {
        uint8_t* p = &pPixels[x * 3];
        grayLine[x] = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
      }
      break;
    case PNG_PIXEL_INDEXED:
      if (palette) {
        if (hasAlpha) {
          for (int x = 0; x < width; x++) {
            uint8_t idx = pPixels[x];
            uint8_t* p = &palette[idx * 3];
            uint8_t gray = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
            uint8_t alpha = palette[768 + idx];
            grayLine[x] = (uint8_t)((gray * alpha + 255 * (255 - alpha)) / 255);
          }
        } else {
          for (int x = 0; x < width; x++) {
            uint8_t* p = &palette[pPixels[x] * 3];
            grayLine[x] = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          }
        }
      } else {
        memcpy(grayLine, pPixels, width);
      }
      break;
    case PNG_PIXEL_GRAY_ALPHA:
      for (int x = 0; x < width; x++) {
        uint8_t gray = pPixels[x * 2];
        uint8_t alpha = pPixels[x * 2 + 1];
        grayLine[x] = (uint8_t)((gray * alpha + 255 * (255 - alpha)) / 255);
      }
      break;
    case PNG_PIXEL_TRUECOLOR_ALPHA:
      for (int x = 0; x < width; x++) {
        uint8_t* p = &pPixels[x * 4];
        uint8_t gray = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
        uint8_t alpha = p[3];
        grayLine[x] = (uint8_t)((gray * alpha + 255 * (255 - alpha)) / 255);
      }
      break;
    default:
      memset(grayLine, 128, width);
      break;
  }
}

int pngDrawCallback(PNGDRAW* pDraw) {
  PngContext* ctx = reinterpret_cast<PngContext*>(pDraw->pUser);
  if (!ctx || !ctx->config || !ctx->renderer || !ctx->grayLineBuffer) return 0;

  int srcY = pDraw->y;
  int dstY = (int)(srcY * ctx->scale);
  if (dstY == ctx->lastDstY) return 1;
  ctx->lastDstY = dstY;

  if (dstY >= ctx->dstHeight) return 1;
  int outY = ctx->config->y + dstY;
  if (outY >= ctx->screenHeight) return 1;

  convertLineToGray(pDraw->pPixels, ctx->grayLineBuffer, ctx->srcWidth, pDraw->iPixelType, pDraw->pPalette,
                    pDraw->iHasAlpha);

  int dstWidth = ctx->dstWidth;
  int outXBase = ctx->config->x;
  int screenWidth = ctx->screenWidth;
  bool useDithering = ctx->config->useDithering;
  bool caching = ctx->caching;

  int srcX = 0;
  int error = 0;

  for (int dstX = 0; dstX < dstWidth; dstX++) {
    int outX = outXBase + dstX;
    if (outX < screenWidth) {
      uint8_t gray = ctx->grayLineBuffer[srcX];
      uint8_t ditheredGray = useDithering ? applyBayerDither4Level(gray, outX, outY) : (gray / 85 > 3 ? 3 : gray / 85);
      drawPixelWithRenderMode(*ctx->renderer, outX, outY, ditheredGray);
      if (caching) ctx->cache.setPixel(outX, outY, ditheredGray);
    }
    error += ctx->srcWidth;
    while (error >= dstWidth) {
      error -= dstWidth;
      srcX++;
    }
  }
  return 1;
}

}  // namespace

bool PngToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_PNG) {
    LOG_ERR("PNG", "Not enough heap for PNG decoder (%u free, need %u)", freeHeap,
            static_cast<unsigned>(MIN_FREE_HEAP_FOR_PNG));
    return false;
  }

  PNG* png = new (std::nothrow) PNG();
  if (!png) {
    LOG_ERR("PNG", "Failed to allocate PNG decoder for dimensions");
    return false;
  }

  int rc = png->open(imagePath.c_str(), pngOpenWithHandle, pngCloseWithHandle, pngReadWithHandle, pngSeekWithHandle,
                     nullptr);
  if (rc != 0) {
    LOG_ERR("PNG", "Failed to open PNG for dimensions: %d", rc);
    delete png;
    return false;
  }

  out.width = png->getWidth();
  out.height = png->getHeight();
  png->close();
  delete png;
  return true;
}

bool PngToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  LOG_DBG("PNG", "Decoding PNG: %s", imagePath.c_str());

  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_PNG) {
    LOG_ERR("PNG", "Not enough heap for PNG decoder (%u free, need %u)", freeHeap,
            static_cast<unsigned>(MIN_FREE_HEAP_FOR_PNG));
    return false;
  }

  PNG* png = new (std::nothrow) PNG();
  if (!png) {
    LOG_ERR("PNG", "Failed to allocate PNG decoder");
    return false;
  }

  PngContext ctx;
  ctx.renderer = &renderer;
  ctx.config = &config;
  ctx.screenWidth = renderer.getScreenWidth();
  ctx.screenHeight = renderer.getScreenHeight();

  int rc = png->open(imagePath.c_str(), pngOpenWithHandle, pngCloseWithHandle, pngReadWithHandle, pngSeekWithHandle,
                     pngDrawCallback);
  if (rc != PNG_SUCCESS) {
    LOG_ERR("PNG", "Failed to open PNG: %d", rc);
    delete png;
    return false;
  }

  if (!validateImageDimensions(png->getWidth(), png->getHeight(), "PNG")) {
    png->close();
    delete png;
    return false;
  }

  ctx.srcWidth = png->getWidth();
  ctx.srcHeight = png->getHeight();

  if (config.useExactDimensions && config.maxWidth > 0 && config.maxHeight > 0) {
    ctx.dstWidth = config.maxWidth;
    ctx.dstHeight = config.maxHeight;
    ctx.scale = (float)ctx.dstWidth / ctx.srcWidth;
  } else {
    float scaleX = (float)config.maxWidth / ctx.srcWidth;
    float scaleY = (float)config.maxHeight / ctx.srcHeight;
    ctx.scale = (scaleX < scaleY) ? scaleX : scaleY;
    if (ctx.scale > 1.0f) ctx.scale = 1.0f;
    ctx.dstWidth = (int)(ctx.srcWidth * ctx.scale);
    ctx.dstHeight = (int)(ctx.srcHeight * ctx.scale);
  }
  ctx.lastDstY = -1;

  LOG_DBG("PNG", "PNG %dx%d -> %dx%d (scale %.2f), bpp: %d", ctx.srcWidth, ctx.srcHeight, ctx.dstWidth, ctx.dstHeight,
          ctx.scale, png->getBpp());

  const int pixelType = png->getPixelType();
  const int requiredInternal = requiredPngInternalBufferBytes(ctx.srcWidth, pixelType);
  if (requiredInternal > PNG_MAX_BUFFERED_PIXELS) {
    LOG_ERR("PNG", "PNG row buffer too small: need %d bytes, configured %d", requiredInternal, PNG_MAX_BUFFERED_PIXELS);
    png->close();
    delete png;
    return false;
  }

  ctx.grayLineBuffer = static_cast<uint8_t*>(malloc(PNG_MAX_BUFFERED_PIXELS / 2));
  if (!ctx.grayLineBuffer) {
    LOG_ERR("PNG", "Failed to allocate gray line buffer");
    png->close();
    delete png;
    return false;
  }

  ctx.caching = !config.cachePath.empty();
  if (ctx.caching && !ctx.cache.allocate(ctx.dstWidth, ctx.dstHeight, config.x, config.y)) {
    LOG_ERR("PNG", "Failed to allocate cache buffer");
    ctx.caching = false;
  }

  unsigned long decodeStart = millis();
  rc = png->decode(&ctx, 0);
  unsigned long decodeTime = millis() - decodeStart;

  free(ctx.grayLineBuffer);
  if (rc != PNG_SUCCESS) {
    LOG_ERR("PNG", "Decode failed: %d", rc);
    png->close();
    delete png;
    return false;
  }

  png->close();
  delete png;
  LOG_DBG("PNG", "PNG decoding complete - time: %lu ms", decodeTime);

  if (ctx.caching) ctx.cache.writeToFile(config.cachePath);
  return true;
}

bool PngToFramebufferConverter::supportsFormat(const std::string& extension) {
  std::string ext = extension;
  for (auto& c : ext) c = tolower(c);
  return (ext == ".png");
}
