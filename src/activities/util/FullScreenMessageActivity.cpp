#include "FullScreenMessageActivity.h"

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "fontIds.h"

void FullScreenMessageActivity::onEnter() {
  Activity::onEnter();

  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (renderer.getScreenHeight() - height) / 2;

  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, top, text.c_str(), true, style);
  renderer.displayBuffer(refreshMode);
}

void FullScreenMessageActivity::loop() {
  if (!onDismiss) {
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onDismiss();
  }
}
