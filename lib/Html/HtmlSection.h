#pragma once

#include <SDCardManager.h>

#include <functional>
#include <memory>
#include <string>

class Page;
class GfxRenderer;

class HtmlSection {
 public:
  HtmlSection(const std::string& htmlPath, const std::string& cachePath, const std::string& contentBasePath,
              GfxRenderer& renderer);
  ~HtmlSection() = default;

  bool loadSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                       uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled,
                       uint32_t sourceSize);
  bool createSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                         uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled,
                         uint32_t sourceSize, const std::function<void()>& progressSetupFn = nullptr,
                         const std::function<void(int)>& progressFn = nullptr);
  std::unique_ptr<Page> loadPageFromSectionFile();
  bool clearCache() const;

  uint16_t pageCount = 0;
  int currentPage = 0;

 private:
  std::string htmlPath;
  std::string cachePath;
  std::string contentBasePath;
  GfxRenderer& renderer;
  std::string filePath;
  FsFile file;

  void writeSectionFileHeader(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                              uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled,
                              uint32_t sourceSize);
  uint32_t onPageComplete(std::unique_ptr<Page> page);
};
