#include "ValidateSleepImagesActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "activities/TaskShutdown.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ValidateSleepImagesActivity::taskTrampoline(void* param) {
  auto* self = static_cast<ValidateSleepImagesActivity*>(param);
  self->displayTaskLoop();
}

void ValidateSleepImagesActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  exitTaskRequested.store(false);
  taskHasExited.store(false);
  state = SCANNING;
  updateRequired = true;

  xTaskCreate(&ValidateSleepImagesActivity::taskTrampoline, "ValidateSleepTask", 4096, this, 1, &displayTaskHandle);
}

void ValidateSleepImagesActivity::onExit() {
  ActivityWithSubactivity::onExit();
  TaskShutdown::requestExit(exitTaskRequested, taskHasExited, displayTaskHandle);
}

void ValidateSleepImagesActivity::displayTaskLoop() {
  while (!exitTaskRequested.load()) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      if (!exitTaskRequested.load()) {
        render();
      }
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

  taskHasExited.store(true);
  vTaskDelete(nullptr);
}

void ValidateSleepImagesActivity::render() {
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Validate Sleep Images", true, EpdFontFamily::BOLD);

  if (state == SCANNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Scanning sleep images...", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == DONE) {
    String resultText = String(validCount) + " valid image" + (validCount == 1 ? "" : "s") + " found";
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, resultText.c_str(), true, EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void ValidateSleepImagesActivity::loop() {
  if (state == SCANNING) {
    LOG_INF("VALIDATE_SLEEP", "Starting sleep image validation");
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    validCount = validateAndCountSleepImages();
    state = DONE;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    LOG_INF("VALIDATE_SLEEP", "Validation complete: %d valid images", validCount);
    return;
  }

  if (state == DONE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      goBack();
    }
  }
}
