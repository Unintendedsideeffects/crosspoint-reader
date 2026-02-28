#include "ValidateSleepImagesActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "fontIds.h"

void ValidateSleepImagesActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  state = SCANNING;
  scanStarted = false;
  validCount = 0;
  requestUpdate();
}

void ValidateSleepImagesActivity::onExit() { ActivityWithSubactivity::onExit(); }

void ValidateSleepImagesActivity::render(RenderLock&&) {
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Validate Sleep Images", true, EpdFontFamily::BOLD);

  if (state == SCANNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Scanning sleep images...", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  String resultText = String(validCount) + " valid image" + (validCount == 1 ? "" : "s") + " found";
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, resultText.c_str(), true, EpdFontFamily::BOLD);

  const auto labels = mappedInput.mapLabels("« Back", "", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void ValidateSleepImagesActivity::loop() {
  if (state == SCANNING) {
    if (!scanStarted) {
      scanStarted = true;
      requestUpdateAndWait();
      LOG_INF("VALIDATE_SLEEP", "Starting sleep image validation");
      validCount = validateAndCountSleepImages();
      state = DONE;
      requestUpdate();
      LOG_INF("VALIDATE_SLEEP", "Validation complete: %d valid images", validCount);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goBack();
  }
}
