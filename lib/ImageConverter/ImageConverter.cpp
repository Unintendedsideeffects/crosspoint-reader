#include "ImageConverter.h"

#include <JpegToBmpConverter.h>
#include <PngToBmpConverter.h>
#include <SdFat.h>

#include <cstring>

ImageConverter::Format ImageConverter::detectFormat(const char* filepath) {
  const char* ext = strrchr(filepath, '.');
  if (!ext) return FORMAT_UNKNOWN;

  // Skip the dot
  ext++;

  // Case-insensitive comparison
  if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
    return FORMAT_JPEG;
  }

  if (strcasecmp(ext, "png") == 0) {
    return FORMAT_PNG;
  }

  return FORMAT_UNKNOWN;
}

bool ImageConverter::convertToBmpStream(FsFile& imageFile, Format format, Print& bmpOut, int targetWidth,
                                        int targetHeight, bool crop) {
  switch (format) {
    case FORMAT_JPEG:
      return JpegToBmpConverter::jpegFileToBmpStreamWithSize(imageFile, bmpOut, targetWidth, targetHeight);

    case FORMAT_PNG:
      return PngToBmpConverter::pngFileToBmpStreamWithSize(imageFile, bmpOut, targetWidth, targetHeight);

    default:
      return false;
  }
}

bool ImageConverter::convertTo1BitBmpStream(FsFile& imageFile, Format format, Print& bmpOut, int targetWidth,
                                            int targetHeight) {
  switch (format) {
    case FORMAT_JPEG:
      return JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(imageFile, bmpOut, targetWidth, targetHeight);

    case FORMAT_PNG:
      return PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(imageFile, bmpOut, targetWidth, targetHeight);

    default:
      return false;
  }
}
