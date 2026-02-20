#include "JpegToFramebufferConverter.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <picojpeg.h>

#include "PixelCache.h"

// Context for picojpeg callbacks
struct JpegContext {
  FsFile* file;
  GfxRenderer* renderer;
  int x;
  int y;
  int maxWidth;
  int maxHeight;
  float scale;
  bool dither;
  bool grayscale;
  PixelCache* cache;
};

// Callback for picojpeg to read bytes
unsigned char JpegToFramebufferConverter::jpegReadCallback(unsigned char* pBuf, unsigned char buf_size,
                                                           unsigned char* pBytes_actually_read, void* pCallback_data) {
  JpegContext* context = static_cast<JpegContext*>(pCallback_data);
  size_t read = context->file->read(pBuf, buf_size);
  *pBytes_actually_read = (unsigned char)read;
  return read == buf_size ? 0 : PJPG_STREAM_READ_ERROR;
}

bool JpegToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  FsFile file;
  if (!Storage.openFileForRead("JPG", imagePath, file)) {
    LOG_ERR("JPG", "Failed to open file for dimensions: %s", imagePath.c_str());
    return false;
  }

  pjpeg_image_info_t imageInfo;
  JpegContext context = {&file, nullptr, 0, 0, 0, 0, 1.0f, false, false, nullptr};
  int status = pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0);
  file.close();

  if (status != 0) {
    LOG_ERR("JPG", "Failed to init JPEG for dimensions: %d", status);
    return false;
  }

  out.width = imageInfo.m_width;
  out.height = imageInfo.m_height;
  LOG_DBG("JPG", "Image dimensions: %dx%d", out.width, out.height);
  return true;
}

bool JpegToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                     const RenderConfig& config) {
  LOG_DBG("JPG", "Decoding JPEG: %s", imagePath.c_str());

  FsFile file;
  if (!Storage.openFileForRead("JPG", imagePath, file)) {
    LOG_ERR("JPG", "Failed to open file: %s", imagePath.c_str());
    return false;
  }

  JpegContext context = {&file, &renderer, config.x, config.y, config.maxWidth, config.maxHeight};
  pjpeg_image_info_t imageInfo;

  int status = pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0);
  if (status != 0) {
    LOG_ERR("JPG", "picojpeg init failed: %d", status);
    file.close();
    return false;
  }

  // Calculate scaling factor
  int destWidth = imageInfo.m_width;
  int destHeight = imageInfo.m_height;
  float scale = 1.0f;

  if (config.maxWidth > 0 && destWidth > config.maxWidth) {
    scale = (float)config.maxWidth / destWidth;
    destWidth = config.maxWidth;
    destHeight = (int)(imageInfo.m_height * scale);
  }

  if (config.maxHeight > 0 && destHeight > config.maxHeight) {
    float hScale = (float)config.maxHeight / imageInfo.m_height;
    if (hScale < scale) {
      scale = hScale;
      destWidth = (int)(imageInfo.m_width * scale);
      destHeight = (int)(imageInfo.m_height * scale);
    }
  }

  LOG_DBG("JPG", "JPEG %dx%d -> %dx%d (scale %.2f), scan type: %d, MCU: %dx%d", imageInfo.m_width, imageInfo.m_height,
          destWidth, destHeight, scale, imageInfo.m_scanType, imageInfo.m_MCUWidth, imageInfo.m_MCUHeight);

  if (!imageInfo.m_pMCUBufR || !imageInfo.m_pMCUBufG || !imageInfo.m_pMCUBufB) {
    LOG_ERR("JPG", "Null buffer pointers in imageInfo");
    file.close();
    return false;
  }

  context.scale = scale;
  context.dither = config.useDithering;
  context.grayscale = config.useGrayscale;

  PixelCache cache;
  bool caching = !config.cachePath.empty();
  if (caching) {
    if (!cache.allocate(destWidth, destHeight, config.x, config.y)) {
      LOG_ERR("JPG", "Failed to allocate cache buffer, continuing without caching");
      caching = false;
    }
  }
  context.cache = caching ? &cache : nullptr;

  for (;;) {
    status = pjpeg_decode_mcu();
    if (status != 0) {
      if (status == PJPG_STREAM_READ_ERROR) {
        // expected at end of stream?
        break;
      }
      if (status != 0) {
        LOG_ERR("JPG", "MCU decode failed: %d", status);
        file.close();
        return false;
      }
    }
  }

  LOG_DBG("JPG", "Decoding complete");
  file.close();

  // Write cache file if caching was enabled
  if (caching) {
    cache.writeToFile(config.cachePath);
  }

  return true;
}

bool JpegToFramebufferConverter::supportsFormat(const std::string& extension) {
  std::string ext = extension;
  for (auto& c : ext) {
    c = tolower(c);
  }
  return (ext == ".jpg" || ext == ".jpeg");
}
