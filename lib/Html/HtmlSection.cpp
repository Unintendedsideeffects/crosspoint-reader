#include "HtmlSection.h"

#include <SDCardManager.h>
#include <Serialization.h>

#include <vector>

#include "Epub/Page.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "SpiBusMutex.h"

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 1;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(uint32_t) +
                                 sizeof(uint16_t) + sizeof(uint32_t);
constexpr uint32_t MIN_SIZE_FOR_PROGRESS = 50 * 1024;
}  // namespace

HtmlSection::HtmlSection(const std::string& htmlPath, const std::string& cachePath, const std::string& contentBasePath,
                         GfxRenderer& renderer)
    : htmlPath(htmlPath),
      cachePath(cachePath),
      contentBasePath(contentBasePath),
      renderer(renderer),
      filePath(cachePath + "/section.bin") {}

uint32_t HtmlSection::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    Serial.printf("[%lu] [HSC] File not open for writing page %d\n", millis(), pageCount);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    Serial.printf("[%lu] [HSC] Failed to serialize page %d\n", millis(), pageCount);
    return 0;
  }

  pageCount++;
  return position;
}

void HtmlSection::writeSectionFileHeader(int fontId, float lineCompression, bool extraParagraphSpacing,
                                         uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                                         bool hyphenationEnabled, uint32_t sourceSize) {
  if (!file) {
    Serial.printf("[%lu] [HSC] File not open for writing header\n", millis());
    return;
  }

  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(fontId) + sizeof(lineCompression) +
                                   sizeof(extraParagraphSpacing) + sizeof(paragraphAlignment) + sizeof(viewportWidth) +
                                   sizeof(viewportHeight) + sizeof(hyphenationEnabled) + sizeof(sourceSize) +
                                   sizeof(pageCount) + sizeof(uint32_t),
                "Header size mismatch");

  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, extraParagraphSpacing);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, sourceSize);
  serialization::writePod(file, pageCount);                 // Placeholder
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset
}

bool HtmlSection::loadSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing,
                                  uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                                  bool hyphenationEnabled, uint32_t sourceSize) {
  SpiBusMutex::Guard guard;
  if (!SdMan.openFileForRead("HSC", filePath, file)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != SECTION_FILE_VERSION) {
    file.close();
    Serial.printf("[%lu] [HSC] Deserialization failed: Unknown version %u\n", millis(), version);
    clearCache();
    return false;
  }

  int fileFontId;
  uint16_t fileViewportWidth, fileViewportHeight;
  float fileLineCompression;
  bool fileExtraParagraphSpacing;
  uint8_t fileParagraphAlignment;
  bool fileHyphenationEnabled;
  uint32_t fileSourceSize;

  serialization::readPod(file, fileFontId);
  serialization::readPod(file, fileLineCompression);
  serialization::readPod(file, fileExtraParagraphSpacing);
  serialization::readPod(file, fileParagraphAlignment);
  serialization::readPod(file, fileViewportWidth);
  serialization::readPod(file, fileViewportHeight);
  serialization::readPod(file, fileHyphenationEnabled);
  serialization::readPod(file, fileSourceSize);

  if (fontId != fileFontId || lineCompression != fileLineCompression ||
      extraParagraphSpacing != fileExtraParagraphSpacing || paragraphAlignment != fileParagraphAlignment ||
      viewportWidth != fileViewportWidth || viewportHeight != fileViewportHeight ||
      hyphenationEnabled != fileHyphenationEnabled || sourceSize != fileSourceSize) {
    file.close();
    Serial.printf("[%lu] [HSC] Deserialization failed: Parameters do not match\n", millis());
    clearCache();
    return false;
  }

  serialization::readPod(file, pageCount);
  file.close();
  Serial.printf("[%lu] [HSC] Deserialization succeeded: %d pages\n", millis(), pageCount);
  return true;
}

bool HtmlSection::clearCache() const {
  SpiBusMutex::Guard guard;
  if (!SdMan.exists(filePath.c_str())) {
    return true;
  }
  if (!SdMan.remove(filePath.c_str())) {
    Serial.printf("[%lu] [HSC] Failed to clear cache\n", millis());
    return false;
  }
  return true;
}

bool HtmlSection::createSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing,
                                    uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                                    bool hyphenationEnabled, uint32_t sourceSize,
                                    const std::function<void()>& progressSetupFn,
                                    const std::function<void(int)>& progressFn) {
  SpiBusMutex::Guard guard;

  if (!SdMan.exists(cachePath.c_str())) {
    SdMan.mkdir(cachePath.c_str());
  }

  if (!SdMan.openFileForWrite("HSC", filePath, file)) {
    return false;
  }

  writeSectionFileHeader(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled, sourceSize);

  std::vector<uint32_t> lut = {};

  size_t htmlSize = 0;
  {
    FsFile htmlFile;
    if (SdMan.openFileForRead("HSC", htmlPath, htmlFile)) {
      htmlSize = htmlFile.size();
      htmlFile.close();
    }
  }

  if (progressSetupFn && htmlSize >= MIN_SIZE_FOR_PROGRESS) {
    progressSetupFn();
  }

  ChapterHtmlSlimParser visitor(
      htmlPath, renderer, fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
      viewportHeight, hyphenationEnabled,
      [this, &lut](std::unique_ptr<Page> page) { lut.emplace_back(this->onPageComplete(std::move(page))); }, nullptr,
      nullptr, contentBasePath);

  bool success = visitor.parseAndBuildPages();
  if (!success) {
    Serial.printf("[%lu] [HSC] Failed to parse HTML and build pages\n", millis());
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  const uint32_t lutOffset = file.position();
  bool hasFailedLutRecords = false;
  for (const uint32_t& pos : lut) {
    if (pos == 0) {
      hasFailedLutRecords = true;
      break;
    }
    serialization::writePod(file, pos);
  }

  if (hasFailedLutRecords) {
    Serial.printf("[%lu] [HSC] Failed to write LUT due to invalid page positions\n", millis());
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  file.close();
  return true;
}

std::unique_ptr<Page> HtmlSection::loadPageFromSectionFile() {
  SpiBusMutex::Guard guard;
  if (!SdMan.openFileForRead("HSC", filePath, file)) {
    return nullptr;
  }

  file.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);
  file.seek(lutOffset + sizeof(uint32_t) * currentPage);
  uint32_t pagePos;
  serialization::readPod(file, pagePos);
  file.seek(pagePos);

  auto page = Page::deserialize(file);
  file.close();
  return page;
}
