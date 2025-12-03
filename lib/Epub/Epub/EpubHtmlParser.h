#pragma once
#include <tinyxml2.h>

#include <functional>

#include "blocks/TextBlock.h"

class Page;
class EpdRenderer;

class EpubHtmlParser final : public tinyxml2::XMLVisitor {
  const char* filepath;
  EpdRenderer* renderer;
  std::function<void(Page*)> completePageFn;

  bool insideBoldTag = false;
  bool insideItalicTag = false;
  TextBlock* currentTextBlock = nullptr;
  Page* currentPage = nullptr;

  void startNewTextBlock(BLOCK_STYLE style);
  void makePages();

  // xml parser callbacks
  bool VisitEnter(const tinyxml2::XMLElement& element, const tinyxml2::XMLAttribute* firstAttribute) override;
  bool Visit(const tinyxml2::XMLText& text) override;
  bool VisitExit(const tinyxml2::XMLElement& element) override;
  // xml parser callbacks
 public:
  explicit EpubHtmlParser(const char* filepath, EpdRenderer* renderer, const std::function<void(Page*)>& completePageFn)
      : filepath(filepath), renderer(renderer), completePageFn(completePageFn) {}
  ~EpubHtmlParser() override = default;
  bool parseAndBuildPages();
};
