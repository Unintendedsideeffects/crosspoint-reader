#include "FullScreenMessageScreen.h"

#include <EpdRenderer.h>

void FullScreenMessageScreen::onEnter() {
  const auto width = renderer->getTextWidth(text.c_str(), bold, italic);
  const auto height = renderer->getLineHeight();
  const auto left = (renderer->getPageWidth() - width) / 2;
  const auto top = (renderer->getPageHeight() - height) / 2;

  renderer->clearScreen(invert);
  renderer->drawText(left, top, text.c_str(), bold, italic, invert ? 0 : 1);
  // If inverted, do a full screen update to ensure no ghosting
  renderer->flushDisplay(!invert);
}
