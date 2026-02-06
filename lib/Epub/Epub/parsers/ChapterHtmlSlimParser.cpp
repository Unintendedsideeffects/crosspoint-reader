#include "ChapterHtmlSlimParser.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <ImageConverter.h>
#include <SDCardManager.h>
#include <expat.h>

#include <cstring>

#include "../../Epub.h"
#include "../Page.h"

// MemoryPrint - a Print adapter that writes to a std::vector<uint8_t>
class MemoryPrint : public Print {
  std::vector<uint8_t>& buffer;

 public:
  explicit MemoryPrint(std::vector<uint8_t>& buf) : buffer(buf) {}
  size_t write(uint8_t b) override {
    buffer.push_back(b);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    buffer.insert(buffer.end(), buf, buf + size);
    return size;
  }
};

// MemoryFile - wraps a memory buffer to look like an FsFile for reading
class MemoryFile : public Stream {
  const uint8_t* data;
  size_t dataSize;
  size_t pos;

 public:
  MemoryFile(const uint8_t* d, size_t s) : data(d), dataSize(s), pos(0) {}
  int available() override { return dataSize - pos; }
  int read() override { return (pos < dataSize) ? data[pos++] : -1; }
  int peek() override { return (pos < dataSize) ? data[pos] : -1; }
  size_t readBytes(char* buffer, size_t length) override {
    size_t toRead = std::min(length, dataSize - pos);
    memcpy(buffer, data + pos, toRead);
    pos += toRead;
    return toRead;
  }
  size_t write(uint8_t) override { return 0; }
  void flush() override {}
  bool seek(size_t position) {
    if (position <= dataSize) {
      pos = position;
      return true;
    }
    return false;
  }
  size_t position() const { return pos; }
  size_t size() const { return dataSize; }
};

const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr int NUM_HEADER_TAGS = sizeof(HEADER_TAGS) / sizeof(HEADER_TAGS[0]);

// Minimum file size (in bytes) to show indexing popup - smaller chapters don't benefit from it
constexpr size_t MIN_SIZE_FOR_POPUP = 50 * 1024;  // 50KB

const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote"};
constexpr int NUM_BLOCK_TAGS = sizeof(BLOCK_TAGS) / sizeof(BLOCK_TAGS[0]);

const char* BOLD_TAGS[] = {"b", "strong"};
constexpr int NUM_BOLD_TAGS = sizeof(BOLD_TAGS) / sizeof(BOLD_TAGS[0]);

const char* ITALIC_TAGS[] = {"i", "em", "mark"};
constexpr int NUM_ITALIC_TAGS = sizeof(ITALIC_TAGS) / sizeof(ITALIC_TAGS[0]);

const char* UNDERLINE_TAGS[] = {"u", "ins"};
constexpr int NUM_UNDERLINE_TAGS = sizeof(UNDERLINE_TAGS) / sizeof(UNDERLINE_TAGS[0]);

const char* IMAGE_TAGS[] = {"img"};
constexpr int NUM_IMAGE_TAGS = sizeof(IMAGE_TAGS) / sizeof(IMAGE_TAGS[0]);

const char* PRE_TAGS[] = {"pre"};
constexpr int NUM_PRE_TAGS = sizeof(PRE_TAGS) / sizeof(PRE_TAGS[0]);

const char* SKIP_TAGS[] = {"head"};
constexpr int NUM_SKIP_TAGS = sizeof(SKIP_TAGS) / sizeof(SKIP_TAGS[0]);

bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

namespace {
bool decode1BitBmpToRaw(const std::vector<uint8_t>& bmpData, std::vector<uint8_t>& rawData, uint16_t& outWidth,
                        uint16_t& outHeight) {
  if (bmpData.size() < 54) {
    return false;
  }
  if (bmpData[0] != 'B' || bmpData[1] != 'M') {
    return false;
  }

  const uint32_t pixelOffset = bmpData[10] | (bmpData[11] << 8) | (bmpData[12] << 16) | (bmpData[13] << 24);
  const int32_t width = static_cast<int32_t>(bmpData[18]) | (static_cast<int32_t>(bmpData[19]) << 8) |
                        (static_cast<int32_t>(bmpData[20]) << 16) | (static_cast<int32_t>(bmpData[21]) << 24);
  int32_t height = static_cast<int32_t>(bmpData[22]) | (static_cast<int32_t>(bmpData[23]) << 8) |
                   (static_cast<int32_t>(bmpData[24]) << 16) | (static_cast<int32_t>(bmpData[25]) << 24);
  const bool topDown = height < 0;
  if (height < 0) {
    height = -height;
  }

  const uint16_t bitsPerPixel = bmpData[28] | (bmpData[29] << 8);
  if (bitsPerPixel != 1 || width <= 0 || height <= 0) {
    return false;
  }

  const uint32_t rowBytesBmp = ((static_cast<uint32_t>(width) + 31) / 32) * 4;
  const uint32_t rowBytesRaw = (static_cast<uint32_t>(width) + 7) / 8;
  const uint64_t requiredSize =
      static_cast<uint64_t>(pixelOffset) + static_cast<uint64_t>(rowBytesBmp) * static_cast<uint64_t>(height);
  if (requiredSize > bmpData.size()) {
    return false;
  }

  rawData.assign(rowBytesRaw * static_cast<uint32_t>(height), 0);
  for (int32_t row = 0; row < height; row++) {
    const uint32_t srcRow = static_cast<uint32_t>(topDown ? row : (height - 1 - row));
    const uint32_t srcOffset = pixelOffset + srcRow * rowBytesBmp;
    const uint32_t dstOffset = static_cast<uint32_t>(row) * rowBytesRaw;
    memcpy(rawData.data() + dstOffset, bmpData.data() + srcOffset, rowBytesRaw);
  }

  outWidth = static_cast<uint16_t>(width);
  outHeight = static_cast<uint16_t>(height);
  return true;
}
}  // namespace

// given the start and end of a tag, check to see if it matches a known tag
bool matches(const char* tag_name, const char* possible_tags[], const int possible_tag_count) {
  for (int i = 0; i < possible_tag_count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

bool isHeaderOrBlock(const char* name) {
  return matches(name, HEADER_TAGS, NUM_HEADER_TAGS) || matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS);
}

// Update effective bold/italic/underline based on block style and inline style stack
void ChapterHtmlSlimParser::updateEffectiveInlineStyle() {
  // Start with block-level styles
  effectiveBold = currentCssStyle.hasFontWeight() && currentCssStyle.fontWeight == CssFontWeight::Bold;
  effectiveItalic = currentCssStyle.hasFontStyle() && currentCssStyle.fontStyle == CssFontStyle::Italic;
  effectiveUnderline =
      currentCssStyle.hasTextDecoration() && currentCssStyle.textDecoration == CssTextDecoration::Underline;

  // Apply inline style stack in order
  for (const auto& entry : inlineStyleStack) {
    if (entry.hasBold) {
      effectiveBold = entry.bold;
    }
    if (entry.hasItalic) {
      effectiveItalic = entry.italic;
    }
    if (entry.hasUnderline) {
      effectiveUnderline = entry.underline;
    }
  }
}

// flush the contents of partWordBuffer to currentTextBlock
void ChapterHtmlSlimParser::flushPartWordBuffer() {
  // Determine font style from depth-based tracking and CSS effective style
  const bool isBold = boldUntilDepth < depth || effectiveBold;
  const bool isItalic = italicUntilDepth < depth || effectiveItalic;
  const bool isUnderline = underlineUntilDepth < depth || effectiveUnderline;

  // Combine style flags using bitwise OR
  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (isBold) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::BOLD);
  }
  if (isItalic) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::ITALIC);
  }
  if (isUnderline) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::UNDERLINE);
  }

  // flush the buffer
  partWordBuffer[partWordBufferIndex] = '\0';
  currentTextBlock->addWord(partWordBuffer, fontStyle, false, nextWordContinues);
  partWordBufferIndex = 0;
  nextWordContinues = false;
}

// start a new text block if needed
void ChapterHtmlSlimParser::startNewTextBlock(const BlockStyle& blockStyle) {
  nextWordContinues = false;  // New block = new paragraph, no continuation
  if (currentTextBlock) {
    // already have a text block running and it is empty - just reuse it
    if (currentTextBlock->isEmpty()) {
      // Merge with existing block style to accumulate CSS styling from parent block elements.
      // This handles cases like <div style="margin-bottom:2em"><h1>text</h1></div> where the
      // div's margin should be preserved, even though it has no direct text content.
      currentTextBlock->setBlockStyle(currentTextBlock->getBlockStyle().getCombinedBlockStyle(blockStyle));
      return;
    }

    makePages();
  }
  currentTextBlock.reset(new ParsedText(extraParagraphSpacing, hyphenationEnabled, blockStyle));
}

void XMLCALL ChapterHtmlSlimParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  // Extract class and style attributes for CSS processing
  std::string classAttr;
  std::string styleAttr;
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "class") == 0) {
        classAttr = atts[i + 1];
      } else if (strcmp(atts[i], "style") == 0) {
        styleAttr = atts[i + 1];
      }
    }
  }

  auto centeredBlockStyle = BlockStyle();
  centeredBlockStyle.textAlignDefined = true;
  centeredBlockStyle.alignment = CssTextAlign::Center;

  // Special handling for tables - show placeholder text instead of dropping silently
  if (strcmp(name, "table") == 0) {
    // Add placeholder text
    self->startNewTextBlock(TextBlock::CENTER_ALIGN);
    if (self->currentTextBlock) {
      self->currentTextBlock->addWord("[Table omitted]", EpdFontFamily::ITALIC);
    }

    // Skip table contents
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  if (matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    std::string src;
    std::string alt = "Image";

    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "src") == 0) {
          src = atts[i + 1];
        } else if (strcmp(atts[i], "alt") == 0) {
          alt = atts[i + 1];
        }
      }
    }

    // Try to process the actual image
    if (!src.empty()) {
      self->processImage(src.c_str(), alt.c_str());
    } else {
      // Fallback to placeholder text
      Serial.printf("[%lu] [EHP] Image placeholder: %s\n", millis(), alt.c_str());
      self->startNewTextBlock(TextBlock::CENTER_ALIGN);
      std::string placeholder = "[Image: " + alt + "] ";
      self->italicUntilDepth = min(self->italicUntilDepth, self->depth);
      self->depth += 1;
      self->characterData(userData, placeholder.c_str(), placeholder.length());
      return;
    }

    self->depth += 1;
    return;
  }

  if (matches(name, SKIP_TAGS, NUM_SKIP_TAGS)) {
    // start skip
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // Skip blocks with role="doc-pagebreak" and epub:type="pagebreak"
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "role") == 0 && strcmp(atts[i + 1], "doc-pagebreak") == 0 ||
          strcmp(atts[i], "epub:type") == 0 && strcmp(atts[i + 1], "pagebreak") == 0) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }
    }
  }

  if (matches(name, PRE_TAGS, NUM_PRE_TAGS)) {
    self->startNewTextBlock(TextBlock::LEFT_ALIGN);
    self->preUntilDepth = std::min(self->preUntilDepth, self->depth);
  } else if (matches(name, HEADER_TAGS, NUM_HEADER_TAGS)) {
    self->startNewTextBlock(TextBlock::CENTER_ALIGN);
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
  } else if (matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
    if (strcmp(name, "br") == 0) {
      if (self->partWordBufferIndex > 0) {
        // flush word preceding <br/> to currentTextBlock before calling startNewTextBlock
        self->flushPartWordBuffer();
      }
      self->startNewTextBlock(self->currentTextBlock->getStyle());
    } else {
      self->startNewTextBlock((TextBlock::Style)self->paragraphAlignment);
      if (strcmp(name, "li") == 0) {
        self->currentTextBlock->addWord("\xe2\x80\xa2", EpdFontFamily::REGULAR);
      }
    }
  } else if (matches(name, BOLD_TAGS, NUM_BOLD_TAGS)) {
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
  } else if (matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
  }

  self->depth += 1;
}

void XMLCALL ChapterHtmlSlimParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    return;
  }

  for (int i = 0; i < len; i++) {
    if (isWhitespace(s[i])) {
      if (self->preUntilDepth < self->depth) {
        // Inside PRE block: preserve whitespace
        if (s[i] == '\n') {
          if (self->partWordBufferIndex > 0) self->flushPartWordBuffer();

          if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
            // Force a blank line by adding a space
            self->currentTextBlock->addWord(" ", EpdFontFamily::REGULAR);
          }
          self->startNewTextBlock(self->currentTextBlock ? self->currentTextBlock->getStyle() : TextBlock::LEFT_ALIGN);
        } else if (s[i] == '\r') {
          // Ignore CR, rely on LF
        } else {
          // Space or Tab
          if (self->partWordBufferIndex >= MAX_WORD_SIZE) self->flushPartWordBuffer();

          char c = (s[i] == '\t') ? ' ' : s[i];  // Convert tab to space
          self->partWordBuffer[self->partWordBufferIndex++] = c;

          if (s[i] == '\t' && self->partWordBufferIndex < MAX_WORD_SIZE) {
            // Add extra space for tab (2 spaces total)
            self->partWordBuffer[self->partWordBufferIndex++] = ' ';
          }
        }
        continue;
      }

      // Currently looking at whitespace, if there's anything in the partWordBuffer, flush it
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      // Whitespace is a real word boundary — reset continuation state
      self->nextWordContinues = false;
      // Skip the whitespace char
      continue;
    }

    // Skip Zero Width No-Break Space / BOM (U+FEFF) = 0xEF 0xBB 0xBF
    const XML_Char FEFF_BYTE_1 = static_cast<XML_Char>(0xEF);
    const XML_Char FEFF_BYTE_2 = static_cast<XML_Char>(0xBB);
    const XML_Char FEFF_BYTE_3 = static_cast<XML_Char>(0xBF);

    if (s[i] == FEFF_BYTE_1) {
      // Check if the next two bytes complete the 3-byte sequence
      if ((i + 2 < len) && (s[i + 1] == FEFF_BYTE_2) && (s[i + 2] == FEFF_BYTE_3)) {
        // Sequence 0xEF 0xBB 0xBF found!
        i += 2;    // Skip the next two bytes
        continue;  // Move to the next iteration
      }
    }

    // If we're about to run out of space, then cut the word off and start a new one
    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      self->flushPartWordBuffer();
    }

    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }

  // If we have > 750 words buffered up, perform the layout and consume out all but the last line
  // There should be enough here to build out 1-2 full pages and doing this will free up a lot of
  // memory.
  // Spotted when reading Intermezzo, there are some really long text blocks in there.
  if (self->currentTextBlock->size() > 750) {
    Serial.printf("[%lu] [EHP] Text block too long, splitting into multiple pages\n", millis());
    self->currentTextBlock->layoutAndExtractLines(
        self->renderer, self->fontId, self->viewportWidth,
        [self](const std::shared_ptr<TextBlock>& textBlock) { self->addLineToPage(textBlock); }, false);
  }
}

void XMLCALL ChapterHtmlSlimParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  if (self->partWordBufferIndex > 0) {
    // Only flush out part word buffer if we're closing a block tag or are at the top of the HTML file.
    // We don't want to flush out content when closing inline tags like <span>.
    // Currently this also flushes out on closing <b> and <i> tags, but they are line tags so that shouldn't happen,
    // text styling needs to be overhauled to fix it.
    const bool shouldBreakText = matches(name, BLOCK_TAGS, NUM_BLOCK_TAGS) ||
                                 matches(name, HEADER_TAGS, NUM_HEADER_TAGS) || matches(name, PRE_TAGS, NUM_PRE_TAGS) ||
                                 matches(name, BOLD_TAGS, NUM_BOLD_TAGS) ||
                                 matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS) || self->depth == 1;

    const bool styleWillChange = willPopStyleStack || willClearBold || willClearItalic || willClearUnderline;
    const bool headerOrBlockTag = isHeaderOrBlock(name);

    // Flush buffer with current style BEFORE any style changes
    if (self->partWordBufferIndex > 0) {
      // Flush if style will change OR if we're closing a block/structural element
      const bool isInlineTag = !headerOrBlockTag && strcmp(name, "table") != 0 &&
                               !matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS) && self->depth != 1;
      const bool shouldFlush = styleWillChange || headerOrBlockTag || matches(name, BOLD_TAGS, NUM_BOLD_TAGS) ||
                               matches(name, ITALIC_TAGS, NUM_ITALIC_TAGS) ||
                               matches(name, UNDERLINE_TAGS, NUM_UNDERLINE_TAGS) || strcmp(name, "table") == 0 ||
                               matches(name, IMAGE_TAGS, NUM_IMAGE_TAGS) || self->depth == 1;

      if (shouldFlush) {
        self->flushPartWordBuffer();
        // If closing an inline element, the next word fragment continues the same visual word
        if (isInlineTag) {
          self->nextWordContinues = true;
        }
      }
    }

    self->depth -= 1;

    // Leaving skip
    if (self->skipUntilDepth == self->depth) {
      self->skipUntilDepth = INT_MAX;
    }

    // Leaving pre
    if (self->preUntilDepth == self->depth) {
      self->preUntilDepth = INT_MAX;
    }

    // Leaving bold
    if (self->boldUntilDepth == self->depth) {
      self->boldUntilDepth = INT_MAX;
    }

    // Leaving italic tag
    if (self->italicUntilDepth == self->depth) {
      self->italicUntilDepth = INT_MAX;
    }

    // Leaving underline tag
    if (self->underlineUntilDepth == self->depth) {
      self->underlineUntilDepth = INT_MAX;
    }

    // Pop from inline style stack if we pushed an entry at this depth
    // This handles all inline elements: b, i, u, span, etc.
    if (!self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth) {
      self->inlineStyleStack.pop_back();
      self->updateEffectiveInlineStyle();
    }

    // Clear block style when leaving header or block elements
    if (headerOrBlockTag) {
      self->currentCssStyle.reset();
      self->updateEffectiveInlineStyle();
    }
  }

  bool ChapterHtmlSlimParser::parseAndBuildPages() {
    auto paragraphAlignmentBlockStyle = BlockStyle();
    paragraphAlignmentBlockStyle.textAlignDefined = true;
    // Resolve None sentinel to Justify for initial block (no CSS context yet)
    const auto align = (this->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                           ? CssTextAlign::Justify
                           : static_cast<CssTextAlign>(this->paragraphAlignment);
    paragraphAlignmentBlockStyle.alignment = align;
    startNewTextBlock(paragraphAlignmentBlockStyle);

    const XML_Parser parser = XML_ParserCreate(nullptr);
    int done;

    if (!parser) {
      Serial.printf("[%lu] [EHP] Couldn't allocate memory for parser\n", millis());
      return false;
    }

    FsFile file;
    if (!SdMan.openFileForRead("EHP", filepath, file)) {
      XML_ParserFree(parser);
      return false;
    }

    // Get file size to decide whether to show indexing popup.
    if (popupFn && file.size() >= MIN_SIZE_FOR_POPUP) {
      popupFn();
    }

    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterData);

    do {
      void* const buf = XML_GetBuffer(parser, 1024);
      if (!buf) {
        Serial.printf("[%lu] [EHP] Couldn't allocate memory for buffer\n", millis());
        XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
        XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
        XML_SetCharacterDataHandler(parser, nullptr);
        XML_ParserFree(parser);
        file.close();
        return false;
      }

      const size_t len = file.read(buf, 1024);

      if (len == 0 && file.available() > 0) {
        Serial.printf("[%lu] [EHP] File read error\n", millis());
        XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
        XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
        XML_SetCharacterDataHandler(parser, nullptr);
        XML_ParserFree(parser);
        file.close();
        return false;
      }

      done = file.available() == 0;

      if (XML_ParseBuffer(parser, static_cast<int>(len), done) == XML_STATUS_ERROR) {
        Serial.printf("[%lu] [EHP] Parse error at line %lu:\n%s\n", millis(), XML_GetCurrentLineNumber(parser),
                      XML_ErrorString(XML_GetErrorCode(parser)));
        XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
        XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
        XML_SetCharacterDataHandler(parser, nullptr);
        XML_ParserFree(parser);
        file.close();
        return false;
      }
    } while (!done);

    XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
    XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
    XML_SetCharacterDataHandler(parser, nullptr);
    XML_ParserFree(parser);
    file.close();

    // Process last page if there is still text
    if (currentTextBlock) {
      makePages();
      completePageFn(std::move(currentPage));
      currentPage.reset();
      currentTextBlock.reset();
    }

    return true;
  }

  void ChapterHtmlSlimParser::addLineToPage(std::shared_ptr<TextBlock> line) {
    const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;

    if (currentPageNextY + lineHeight > viewportHeight) {
      completePageFn(std::move(currentPage));
      currentPage.reset(new Page());
      currentPageNextY = 0;
    }

    // Apply horizontal left inset (margin + padding) as x position offset
    const int16_t xOffset = line->getBlockStyle().leftInset();
    currentPage->elements.push_back(std::make_shared<PageLine>(line, xOffset, currentPageNextY));
    currentPageNextY += lineHeight;
  }

  void ChapterHtmlSlimParser::makePages() {
    if (!currentTextBlock) {
      Serial.printf("[%lu] [EHP] !! No text block to make pages for !!\n", millis());
      return;
    }

    if (!currentPage) {
      currentPage.reset(new Page());
      currentPageNextY = 0;
    }

    const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;

    // Apply top spacing before the paragraph (stored in pixels)
    const BlockStyle& blockStyle = currentTextBlock->getBlockStyle();
    if (blockStyle.marginTop > 0) {
      currentPageNextY += blockStyle.marginTop;
    }
    if (blockStyle.paddingTop > 0) {
      currentPageNextY += blockStyle.paddingTop;
    }

    // Calculate effective width accounting for horizontal margins/padding
    const int horizontalInset = blockStyle.totalHorizontalInset();
    const uint16_t effectiveWidth =
        (horizontalInset < viewportWidth) ? static_cast<uint16_t>(viewportWidth - horizontalInset) : viewportWidth;

    currentTextBlock->layoutAndExtractLines(
        renderer, fontId, effectiveWidth,
        [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); });

    // Apply bottom spacing after the paragraph (stored in pixels)
    if (blockStyle.marginBottom > 0) {
      currentPageNextY += blockStyle.marginBottom;
    }
    if (blockStyle.paddingBottom > 0) {
      currentPageNextY += blockStyle.paddingBottom;
    }

    // Extra paragraph spacing if enabled (default behavior)
    if (extraParagraphSpacing) {
      currentPageNextY += lineHeight / 2;
    }
  }

  void ChapterHtmlSlimParser::addImageToPage(std::shared_ptr<PageImage> image) {
    const int imageHeight = image->getHeight();

    // Start a new page if image won't fit
    if (currentPageNextY + imageHeight > viewportHeight) {
      completePageFn(std::move(currentPage));
      currentPage.reset(new Page());
      currentPageNextY = 0;
    }

    // Center the image horizontally
    const int16_t xPos = (viewportWidth - image->getWidth()) / 2;

    // Update position and add to page
    image->xPos = xPos;
    image->yPos = currentPageNextY;
    currentPage->elements.push_back(image);
    currentPageNextY += imageHeight;

    // Add some spacing after image
    const int lineHeight = renderer.getLineHeight(fontId) * lineCompression;
    currentPageNextY += lineHeight / 2;
  }

  void ChapterHtmlSlimParser::processImage(const char* src, const char* alt) {
    Serial.printf("[%lu] [EHP] Processing image: %s\n", millis(), src);

    if (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0) {
      Serial.printf("[%lu] [EHP] Remote image unsupported: %s\n", millis(), src);
      startNewTextBlock(TextBlock::CENTER_ALIGN);
      std::string placeholder = "[Image: " + std::string(alt) + "]";
      currentTextBlock->addWord(placeholder.c_str(), EpdFontFamily::ITALIC);
      return;
    }
    if (strncmp(src, "data:", 5) == 0) {
      Serial.printf("[%lu] [EHP] Data URL image unsupported\n", millis());
      startNewTextBlock(TextBlock::CENTER_ALIGN);
      currentTextBlock->addWord("[Embedded image]", EpdFontFamily::ITALIC);
      return;
    }

    // Resolve relative path against content base path
    std::string imagePath;
    if (src[0] == '/') {
      imagePath = src;
    } else {
      imagePath = FsHelpers::normalisePath(contentBasePath + src);
    }
    Serial.printf("[%lu] [EHP] Resolved image path: %s\n", millis(), imagePath.c_str());

    // Detect format
    auto format = ImageConverter::detectFormat(imagePath.c_str());
    if (format == ImageConverter::FORMAT_UNKNOWN) {
      // Unsupported format - show placeholder
      Serial.printf("[%lu] [EHP] Unsupported image format: %s\n", millis(), src);
      startNewTextBlock(TextBlock::CENTER_ALIGN);
      std::string placeholder = "[Image: " + std::string(alt) + "]";
      currentTextBlock->addWord(placeholder.c_str(), EpdFontFamily::ITALIC);
      return;
    }

    // Flush any pending text before adding image
    if (currentTextBlock && !currentTextBlock->isEmpty()) {
      makePages();
      currentTextBlock.reset(
          new ParsedText((TextBlock::Style)paragraphAlignment, extraParagraphSpacing, hyphenationEnabled));
    }

    // Ensure current page exists
    if (!currentPage) {
      currentPage.reset(new Page());
      currentPageNextY = 0;
    }

    // Calculate max dimensions for inline image (max half viewport height for inline)
    const int maxWidth = viewportWidth;
    const int maxHeight = viewportHeight / 2;

    FsFile tempImage;
    std::string tempImagePath;
    if (epub) {
      // Read image data from EPUB into a temp file (converter expects FsFile)
      size_t imageDataSize = 0;
      uint8_t* imageData = epub->readItemContentsToBytes(imagePath, &imageDataSize);
      if (!imageData || imageDataSize == 0) {
        Serial.printf("[%lu] [EHP] Failed to read image from EPUB: %s\n", millis(), imagePath.c_str());
        startNewTextBlock(TextBlock::CENTER_ALIGN);
        std::string placeholder = "[Image: " + std::string(alt) + "]";
        currentTextBlock->addWord(placeholder.c_str(), EpdFontFamily::ITALIC);
        return;
      }

      tempImagePath = epub->getCachePath() + "/.tmp_image";
      if (!SdMan.openFileForWrite("EHP", tempImagePath, tempImage)) {
        Serial.printf("[%lu] [EHP] Failed to create temp image file\n", millis());
        free(imageData);
        startNewTextBlock(TextBlock::CENTER_ALIGN);
        currentTextBlock->addWord("[Image failed]", EpdFontFamily::ITALIC);
        return;
      }
      const size_t bytesWritten = tempImage.write(imageData, imageDataSize);
      if (bytesWritten != imageDataSize) {
        Serial.printf("[%lu] [EHP] Failed to write temp image data\n", millis());
        tempImage.close();
        free(imageData);
        SdMan.remove(tempImagePath.c_str());
        startNewTextBlock(TextBlock::CENTER_ALIGN);
        currentTextBlock->addWord("[Image failed]", EpdFontFamily::ITALIC);
        return;
      }
      tempImage.close();
      free(imageData);

      if (!SdMan.openFileForRead("EHP", tempImagePath, tempImage)) {
        Serial.printf("[%lu] [EHP] Failed to open temp image file for reading\n", millis());
        SdMan.remove(tempImagePath.c_str());
        startNewTextBlock(TextBlock::CENTER_ALIGN);
        currentTextBlock->addWord("[Image failed]", EpdFontFamily::ITALIC);
        return;
      }
    } else {
      if (!SdMan.openFileForRead("EHP", imagePath, tempImage)) {
        Serial.printf("[%lu] [EHP] Failed to open image file: %s\n", millis(), imagePath.c_str());
        startNewTextBlock(TextBlock::CENTER_ALIGN);
        std::string placeholder = "[Image: " + std::string(alt) + "]";
        currentTextBlock->addWord(placeholder.c_str(), EpdFontFamily::ITALIC);
        return;
      }
    }

    // Convert to 1-bit BMP for raw rendering
    std::vector<uint8_t> bmpData;
    MemoryPrint bmpOut(bmpData);

    bool success = ImageConverter::convertTo1BitBmpStream(tempImage, format, bmpOut, maxWidth, maxHeight, false);
    tempImage.close();
    if (epub && !tempImagePath.empty()) {
      SdMan.remove(tempImagePath.c_str());
    }

    if (!success || bmpData.size() < 54) {
      Serial.printf("[%lu] [EHP] Failed to convert image to 1-bit BMP\n", millis());
      startNewTextBlock(TextBlock::CENTER_ALIGN);
      currentTextBlock->addWord("[Image failed]", EpdFontFamily::ITALIC);
      return;
    }

    // Decode BMP to raw 1-bit data for display
    std::vector<uint8_t> rawData;
    uint16_t bmpWidth = 0;
    uint16_t bmpHeight = 0;
    if (!decode1BitBmpToRaw(bmpData, rawData, bmpWidth, bmpHeight)) {
      Serial.printf("[%lu] [EHP] Failed to decode 1-bit BMP\n", millis());
      startNewTextBlock(TextBlock::CENTER_ALIGN);
      currentTextBlock->addWord("[Image failed]", EpdFontFamily::ITALIC);
      return;
    }

    Serial.printf("[%lu] [EHP] Converted image: %dx%d, %zu bytes (raw)\n", millis(), bmpWidth, bmpHeight,
                  rawData.size());

    // Create PageImage and add to page (raw 1-bit data)
    auto pageImage = std::make_shared<PageImage>(std::move(rawData), bmpWidth, bmpHeight, 0, 0);
    addImageToPage(pageImage);
  }
