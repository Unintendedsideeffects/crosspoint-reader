#pragma once

class FsFile;
class Print;

class ImageConverter {
 public:
  enum Format { FORMAT_UNKNOWN, FORMAT_JPEG, FORMAT_PNG };

  // Detect format from file extension
  static Format detectFormat(const char* filepath);

  // Convert image to BMP stream with scaling
  static bool convertToBmpStream(FsFile& imageFile, Format format, Print& bmpOut, int targetWidth, int targetHeight);

  // Convert image to 1-bit BMP stream (for thumbnails)
  static bool convertTo1BitBmpStream(FsFile& imageFile, Format format, Print& bmpOut, int targetWidth,
                                     int targetHeight);
};
