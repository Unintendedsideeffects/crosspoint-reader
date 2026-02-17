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

void FactoryResetActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  state = WARNING;
  restartAtMs = 0;
  requestUpdate();
}

void FactoryResetActivity::onExit() { ActivityWithSubactivity::onExit(); }

void FactoryResetActivity::render(Activity::RenderLock&& lock) { renderScreen(); }

void FactoryResetActivity::renderScreen() {
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
  LOG_INF("FACTORY_RESET", "Resetting %s", CROSSPOINT_DIR);

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
    LOG_ERR("FACTORY_RESET", "Failed (removed=%d, ensuredDir=%d)", removed, ensuredDir);
    return false;
  }

  LOG_INF("FACTORY_RESET", "Completed successfully");
  return true;
}

void FactoryResetActivity::loop() {
  if (state == WARNING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state = RESETTING;
      requestUpdate();
      yield();

      const bool ok = performFactoryReset();
      if (ok) {
        state = SUCCESS;
        restartAtMs = millis() + RESTART_DELAY_MS;
      } else {
        state = FAILED;
      }
      requestUpdate();
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
