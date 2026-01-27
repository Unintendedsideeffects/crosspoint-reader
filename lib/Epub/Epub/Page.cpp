#include "Page.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <Serialization.h>

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageLine::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize TextBlock pointed to by PageLine
  return block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto tb = TextBlock::deserialize(file);
  return std::unique_ptr<PageLine>(new PageLine(std::move(tb), xPos, yPos));
}

// PageImage implementation
void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  if (bmpData.empty()) {
    return;
  }

  // Create a memory stream from the BMP data and use GfxRenderer's drawImage
  // The BMP data is already in the correct format for display
  // We need to parse the BMP header to get pixel offset, then render the image data
  const int x = xPos + xOffset;
  const int y = yPos + yOffset;

  // Parse BMP header to find pixel data offset
  if (bmpData.size() < 54) {
    Serial.printf("[%lu] [PGE] BMP data too small\n", millis());
    return;
  }

  // Verify BMP signature
  if (bmpData[0] != 'B' || bmpData[1] != 'M') {
    Serial.printf("[%lu] [PGE] Invalid BMP signature\n", millis());
    return;
  }

  // Get pixel data offset from header (offset 10, little-endian 32-bit)
  uint32_t pixelOffset = bmpData[10] | (bmpData[11] << 8) | (bmpData[12] << 16) | (bmpData[13] << 24);

  // Get bits per pixel from header (offset 28, little-endian 16-bit)
  uint16_t bitsPerPixel = bmpData[28] | (bmpData[29] << 8);

  // Draw the image using GfxRenderer's drawImage method
  // Note: drawImage expects raw pixel data, so we pass data starting at pixel offset
  if (pixelOffset < bmpData.size()) {
    renderer.drawImage(bmpData.data() + pixelOffset, x, y, imageWidth, imageHeight);
  }
}

bool PageImage::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);
  serialization::writePod(file, imageWidth);
  serialization::writePod(file, imageHeight);

  // Write BMP data length and data
  const uint32_t dataLen = bmpData.size();
  serialization::writePod(file, dataLen);
  if (dataLen > 0) {
    file.write(bmpData.data(), dataLen);
  }

  return true;
}

std::unique_ptr<PageImage> PageImage::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  uint16_t width;
  uint16_t height;

  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);
  serialization::readPod(file, width);
  serialization::readPod(file, height);

  uint32_t dataLen;
  serialization::readPod(file, dataLen);

  // Sanity check - images shouldn't be huge
  if (dataLen > 1024 * 1024) {  // 1MB max
    Serial.printf("[%lu] [PGE] Image data too large: %u bytes\n", millis(), dataLen);
    return nullptr;
  }

  std::vector<uint8_t> data(dataLen);
  if (dataLen > 0) {
    if (file.read(data.data(), dataLen) != dataLen) {
      Serial.printf("[%lu] [PGE] Failed to read image data\n", millis());
      return nullptr;
    }
  }

  return std::unique_ptr<PageImage>(new PageImage(std::move(data), width, height, xPos, yPos));
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  for (auto& element : elements) {
    element->render(renderer, fontId, xOffset, yOffset);
  }
}

bool Page::serialize(FsFile& file) const {
  const uint16_t count = elements.size();
  serialization::writePod(file, count);

  for (const auto& el : elements) {
    // Write tag using virtual method
    serialization::writePod(file, static_cast<uint8_t>(el->getTag()));

    if (!el->serialize(file)) {
      return false;
    }
  }

  return true;
}

std::unique_ptr<Page> Page::deserialize(FsFile& file) {
  auto page = std::unique_ptr<Page>(new Page());

  uint16_t count;
  serialization::readPod(file, count);

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag;
    serialization::readPod(file, tag);

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      if (pi) {
        page->elements.push_back(std::move(pi));
      } else {
        Serial.printf("[%lu] [PGE] Failed to deserialize PageImage\n", millis());
        return nullptr;
      }
    } else {
      Serial.printf("[%lu] [PGE] Deserialization failed: Unknown tag %u\n", millis(), tag);
      return nullptr;
    }
  }

  return page;
}
