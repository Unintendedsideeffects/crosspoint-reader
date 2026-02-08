#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "FeatureFlags.h"
#include "ImageToFramebufferDecoder.h"

#if ENABLE_IMAGE_SLEEP
class JpegToFramebufferConverter;
class PngToFramebufferConverter;
#endif

class ImageDecoderFactory {
 public:
  static void initialize();
  // Returns non-owning pointer - factory owns the decoder lifetime
  static ImageToFramebufferDecoder* getDecoder(const std::string& imagePath);
  static bool isFormatSupported(const std::string& imagePath);
  static std::vector<std::string> getSupportedFormats();

 private:
#if ENABLE_IMAGE_SLEEP
  static std::unique_ptr<JpegToFramebufferConverter> jpegDecoder;
  static std::unique_ptr<PngToFramebufferConverter> pngDecoder;
#endif
  static bool initialized;
};
