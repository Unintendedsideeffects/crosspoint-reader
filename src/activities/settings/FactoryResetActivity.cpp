#include "FactoryResetActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HardwareSerial.h>

#include "MappedInputManager.h"
#include "SpiBusMutex.h"
#include "activities/TaskShutdown.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char CROSSPOINT_DIR[] = "/.crosspoint";
constexpr unsigned long RESTART_DELAY_MS = 1500;
}  // namespace

void FactoryResetActivity::taskTrampoline(void* param) {
  auto* self = static_cast<FactoryResetActivity*>(param);
  self->displayTaskLoop();
}

void FactoryResetActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  exitTaskRequested.store(false);
  taskHasExited.store(false);
  state = WARNING;
  restartAtMs = 0;
  updateRequired = true;

  xTaskCreate(&FactoryResetActivity::taskTrampoline, "FactoryResetActivityTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void FactoryResetActivity::onExit() {
  ActivityWithSubactivity::onExit();

  TaskShutdown::requestExit(exitTaskRequested, taskHasExited, displayTaskHandle);
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void FactoryResetActivity::displayTaskLoop() {
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

void FactoryResetActivity::render() {
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Factory Reset", true, EpdFontFamily::BOLD);

  if (state == WARNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 90, "This will erase all CrossPoint data.", true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 55, "Settings, WiFi, reading state,", true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 25, "sync credentials, and cache", true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 5, "will be reset.", true);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 45, "Books on the SD card are kept.", true,
                              EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels("« Cancel", "Reset", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == RESETTING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Resetting device...", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, "Factory reset complete", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, "Restarting...");
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, "Factory reset failed", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, "Check SD card and retry");

    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

bool FactoryResetActivity::performFactoryReset() {
  Serial.printf("[%lu] [FACTORY_RESET] Resetting %s\n", millis(), CROSSPOINT_DIR);

  bool removed = true;
  bool ensuredDir = true;
  {
    SpiBusMutex::Guard guard;

    if (Storage.exists(CROSSPOINT_DIR)) {
      removed = Storage.removeDir(CROSSPOINT_DIR);
    }

    if (!Storage.exists(CROSSPOINT_DIR)) {
      ensuredDir = Storage.mkdir(CROSSPOINT_DIR);
    }
  }

  if (!removed || !ensuredDir) {
    Serial.printf("[%lu] [FACTORY_RESET] Failed (removed=%d, ensuredDir=%d)\n", millis(), removed, ensuredDir);
    return false;
  }

  Serial.printf("[%lu] [FACTORY_RESET] Completed successfully\n", millis());
  return true;
}

void FactoryResetActivity::loop() {
  if (state == WARNING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = RESETTING;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);

      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      const bool ok = performFactoryReset();
      if (ok) {
        state = SUCCESS;
        restartAtMs = millis() + RESTART_DELAY_MS;
      } else {
        state = FAILED;
      }
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == SUCCESS) {
    if (restartAtMs > 0 && millis() >= restartAtMs) {
      ESP.restart();
    }
    return;
  }

  if (state == FAILED) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }
}
