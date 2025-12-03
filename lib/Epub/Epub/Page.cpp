#include "Page.h"

#include <HardwareSerial.h>
#include <Serialization.h>

void PageLine::render(EpdRenderer* renderer) { block->render(renderer, 0, yPos); }

void PageLine::serialize(std::ostream& os) {
  serialization::writePod(os, yPos);

  // serialize TextBlock pointed to by PageLine
  block->serialize(os);
}

PageLine* PageLine::deserialize(std::istream& is) {
  int32_t yPos;
  serialization::readPod(is, yPos);

  const auto tb = TextBlock::deserialize(is);
  return new PageLine(tb, yPos);
}

void Page::render(EpdRenderer* renderer) const {
  const auto start = millis();
  for (const auto element : elements) {
    element->render(renderer);
  }
  Serial.printf("Rendered page elements (%u) in %dms\n", elements.size(), millis() - start);
}

void Page::serialize(std::ostream& os) const {
  serialization::writePod(os, nextY);

  const uint32_t count = elements.size();
  serialization::writePod(os, count);

  for (auto* el : elements) {
    // Only PageLine exists currently
    serialization::writePod(os, static_cast<uint8_t>(TAG_PageLine));
    static_cast<PageLine*>(el)->serialize(os);
  }
}

Page* Page::deserialize(std::istream& is) {
  auto* page = new Page();

  serialization::readPod(is, page->nextY);

  uint32_t count;
  serialization::readPod(is, count);

  for (uint32_t i = 0; i < count; i++) {
    uint8_t tag;
    serialization::readPod(is, tag);

    if (tag == TAG_PageLine) {
      auto* pl = PageLine::deserialize(is);
      page->elements.push_back(pl);
    } else {
      throw std::runtime_error("Unknown PageElement tag");
    }
  }

  return page;
}
