#pragma once
#include "blocks/TextBlock.h"

enum PageElementTag : uint8_t {
  TAG_PageLine = 1,
};

// represents something that has been added to a page
class PageElement {
 public:
  int yPos;
  explicit PageElement(const int yPos) : yPos(yPos) {}
  virtual ~PageElement() = default;
  virtual void render(EpdRenderer* renderer) = 0;
  virtual void serialize(std::ostream& os) = 0;
};

// a line from a block element
class PageLine final : public PageElement {
  const TextBlock* block;

 public:
  PageLine(const TextBlock* block, const int yPos) : PageElement(yPos), block(block) {}
  ~PageLine() override { delete block; }
  void render(EpdRenderer* renderer) override;
  void serialize(std::ostream& os) override;
  static PageLine* deserialize(std::istream& is);
};

class Page {
 public:
  int nextY = 0;
  // the list of block index and line numbers on this page
  std::vector<PageElement*> elements;
  void render(EpdRenderer* renderer) const;
  ~Page() {
    for (const auto element : elements) {
      delete element;
    }
  }
  void serialize(std::ostream& os) const;
  static Page* deserialize(std::istream& is);
};
