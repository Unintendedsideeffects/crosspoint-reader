#include "EpubHtmlParser.h"

#include <EpdRenderer.h>
#include <HardwareSerial.h>

#include "Page.h"
#include "htmlEntities.h"

const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr int NUM_HEADER_TAGS = sizeof(HEADER_TAGS) / sizeof(HEADER_TAGS[0]);

const char* BLOCK_TAGS[] = {"p", "li", "div", "br"};
constexpr int NUM_BLOCK_TAGS = sizeof(BLOCK_TAGS) / sizeof(BLOCK_TAGS[0]);

const char* BOLD_TAGS[] = {"b"};
constexpr int NUM_BOLD_TAGS = sizeof(BOLD_TAGS) / sizeof(BOLD_TAGS[0]);

const char* ITALIC_TAGS[] = {"i"};
constexpr int NUM_ITALIC_TAGS = sizeof(ITALIC_TAGS) / sizeof(ITALIC_TAGS[0]);

const char* IMAGE_TAGS[] = {"img"};
constexpr int NUM_IMAGE_TAGS = sizeof(IMAGE_TAGS) / sizeof(IMAGE_TAGS[0]);

const char* SKIP_TAGS[] = {"head", "table"};
constexpr int NUM_SKIP_TAGS = sizeof(SKIP_TAGS) / sizeof(SKIP_TAGS[0]);

// given the start and end of a tag, check to see if it matches a known tag
bool matches(const char* tag_name, const char* possible_tags[], const int possible_tag_count) {
  for (int i = 0; i < possible_tag_count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

// start a new text block if needed
void EpubHtmlParser::startNewTextBlock(const BLOCK_STYLE style) {
  if (currentTextBlock) {
    // already have a text block running and it is empty - just reuse it
    if (currentTextBlock->isEmpty()) {
      currentTextBlock->set_style(style);
      return;
    }

    currentTextBlock->finish();
    makePages();
    delete currentTextBlock;
  }
  currentTextBlock = new TextBlock(style);
}

bool EpubHtmlParser::VisitEnter(const tinyxml2::XMLElement& element, const tinyxml2::XMLAttribute* firstAttribute) {
  const char* tag_name = element.Name();
  if (matches(tag_name, IMAGE_TAGS, NUM_IMAGE_TAGS)) {
    const char* src = element.Attribute("src");
    if (src) {
      // don't leave an empty text block in the list
      // const BLOCK_STYLE style = currentTextBlock->get_style();
      if (currentTextBlock->isEmpty()) {
        delete currentTextBlock;
        currentTextBlock = nullptr;
      }
      // TODO: Fix this
      // blocks.push_back(new ImageBlock(m_base_path + src));
      // start a new text block - with the same style as before
      // startNewTextBlock(style);
    } else {
      // ESP_LOGE(TAG, "Could not find src attribute");
    }
    return false;
  }

  if (matches(tag_name, SKIP_TAGS, NUM_SKIP_TAGS)) {
    return false;
  }

  // Serial.printf("Text: %s\n", element.GetText());

  if (matches(tag_name, HEADER_TAGS, NUM_HEADER_TAGS)) {
    insideBoldTag = true;
    startNewTextBlock(CENTER_ALIGN);
  } else if (matches(tag_name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
    if (strcmp(tag_name, "br") == 0) {
      startNewTextBlock(currentTextBlock->get_style());
    } else {
      startNewTextBlock(JUSTIFIED);
    }
  } else if (matches(tag_name, BOLD_TAGS, NUM_BOLD_TAGS)) {
    insideBoldTag = true;
  } else if (matches(tag_name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
    insideItalicTag = true;
  }
  return true;
}
/// Visit a text node.
bool EpubHtmlParser::Visit(const tinyxml2::XMLText& text) {
  const char* content = text.Value();
  currentTextBlock->addSpan(replaceHtmlEntities(content), insideBoldTag, insideItalicTag);
  return true;
}

bool EpubHtmlParser::VisitExit(const tinyxml2::XMLElement& element) {
  const char* tag_name = element.Name();
  if (matches(tag_name, HEADER_TAGS, NUM_HEADER_TAGS)) {
    insideBoldTag = false;
  } else if (matches(tag_name, BLOCK_TAGS, NUM_BLOCK_TAGS)) {
    // nothing to do
  } else if (matches(tag_name, BOLD_TAGS, NUM_BOLD_TAGS)) {
    insideBoldTag = false;
  } else if (matches(tag_name, ITALIC_TAGS, NUM_ITALIC_TAGS)) {
    insideItalicTag = false;
  }
  return true;
}

bool EpubHtmlParser::parseAndBuildPages() {
  startNewTextBlock(JUSTIFIED);
  tinyxml2::XMLDocument doc(false, tinyxml2::COLLAPSE_WHITESPACE);

  const tinyxml2::XMLError result = doc.LoadFile(filepath);
  if (result != tinyxml2::XML_SUCCESS) {
    Serial.printf("Failed to load file, Error: %s\n", tinyxml2::XMLDocument::ErrorIDToName(result));
    return false;
  }

  doc.Accept(this);
  if (currentTextBlock) {
    makePages();
    completePageFn(currentPage);
    currentPage = nullptr;
    delete currentTextBlock;
    currentTextBlock = nullptr;
  }

  return true;
}

void EpubHtmlParser::makePages() {
  if (!currentTextBlock) {
    Serial.println("!! No text block to make pages for !!");
    return;
  }

  if (!currentPage) {
    currentPage = new Page();
  }

  const int lineHeight = renderer->getLineHeight();
  const int pageHeight = renderer->getPageHeight();

  // Long running task, make sure to let other things happen
  vTaskDelay(1);

  if (currentTextBlock->getType() == TEXT_BLOCK) {
    const auto lines = currentTextBlock->splitIntoLines(renderer);

    for (const auto line : lines) {
      if (currentPage->nextY + lineHeight > pageHeight) {
        completePageFn(currentPage);
        currentPage = new Page();
      }

      currentPage->elements.push_back(new PageLine(line, currentPage->nextY));
      currentPage->nextY += lineHeight;
    }
    // TODO: Fix spacing between paras
    // add some extra line between blocks
    currentPage->nextY += lineHeight / 2;
  }
  // TODO: Image block support
  // if (block->getType() == BlockType::IMAGE_BLOCK) {
  //   ImageBlock *imageBlock = (ImageBlock *)block;
  //   if (y + imageBlock->height > page_height) {
  //     pages.push_back(new Page());
  //     y = 0;
  //   }
  //   pages.back()->elements.push_back(new PageImage(imageBlock, y));
  //   y += imageBlock->height;
  // }
}
