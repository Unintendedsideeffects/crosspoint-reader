#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/TaskShutdown.h"
#include "activities/network/WifiSelectionActivity.h"
#include "fontIds.h"
#include "network/OtaUpdater.h"

void OtaUpdateActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OtaUpdateActivity*>(param);
  self->displayTaskLoop();
}

void OtaUpdateActivity::otaWorkerTrampoline(void* param) {
  auto* self = static_cast<OtaUpdateActivity*>(param);
  self->otaWorkerLoop();
}

void OtaUpdateActivity::dispatchWorker(OtaWorkerCmd cmd) {
  workerCmd.store(cmd);
  if (otaWorkerTaskHandle) {
    xTaskNotify(otaWorkerTaskHandle, 1, eIncrement);
  }
}

void OtaUpdateActivity::otaWorkerLoop() {
  while (!workerExitRequested.load()) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (workerExitRequested.load()) break;

    const auto cmd = workerCmd.exchange(OtaWorkerCmd::NONE);

    if (cmd == OtaWorkerCmd::LOAD_CATALOG) {
      const bool catalogLoaded = updater.loadFeatureStoreCatalog() && updater.hasFeatureStoreCatalog();

      if (catalogLoaded) {
        LOG_INF("OTA", "Feature store catalog loaded, %d bundles", (int)updater.getFeatureStoreEntries().size());
        selectedBundleIndex = 0;
        usingFeatureStore = true;
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = SELECTING_FEATURE_STORE_BUNDLE;
        xSemaphoreGive(renderingMutex);
      } else {
        LOG_WRN("OTA", "Feature store unavailable, falling back to channel OTA");
        usingFeatureStore = false;
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = CHECKING_FOR_UPDATE;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;

        const auto res = updater.checkForUpdate();
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        if (res != OtaUpdater::OK) {
          LOG_ERR("OTA", "Update check failed: %d", res);
          state = FAILED;
        } else if (!updater.isUpdateNewer()) {
          LOG_INF("OTA", "No new update available");
          state = NO_UPDATE;
        } else {
          state = WAITING_CONFIRMATION;
        }
        xSemaphoreGive(renderingMutex);
      }
      updateRequired = true;
    }

    if (cmd == OtaWorkerCmd::CHECK_UPDATE) {
      const auto res = updater.checkForUpdate();
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      if (res != OtaUpdater::OK) {
        LOG_ERR("OTA", "Update check failed: %d", res);
        state = FAILED;
      } else {
        state = WAITING_CONFIRMATION;
      }
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
    }

    if (cmd == OtaWorkerCmd::INSTALL_UPDATE) {
      const auto res = updater.installUpdate();
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      if (res != OtaUpdater::OK) {
        LOG_ERR("OTA", "Update install failed: %d", res);
        state = FAILED;
      } else {
        state = FINISHED;
      }
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
    }
  }

  workerHasExited.store(true);
  vTaskDelete(nullptr);
}

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  exitActivity();

  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
    goBack();
    return;
  }

  LOG_INF("OTA", "WiFi connected, dispatching OTA catalog load to worker");

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = LOADING_FEATURE_STORE;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;

  dispatchWorker(OtaWorkerCmd::LOAD_CATALOG);
}

void OtaUpdateActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  exitTaskRequested.store(false);
  taskHasExited.store(false);
  workerExitRequested.store(false);
  workerHasExited.store(false);
  workerCmd.store(OtaWorkerCmd::NONE);

  xTaskCreate(&OtaUpdateActivity::taskTrampoline, "OtaUpdateActivityTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );

  xTaskCreate(&OtaUpdateActivity::otaWorkerTrampoline, "OtaWorkerTask",
              4096,                 // Stack size — HTTP + JSON parsing needs headroom
              this,                 // Parameters
              1,                    // Priority
              &otaWorkerTaskHandle  // Task handle
  );

  // Turn on WiFi immediately
  LOG_INF("OTA", "Turning on WiFi...");
  WiFi.mode(WIFI_STA);

  // Launch WiFi selection subactivity
  LOG_INF("OTA", "Launching WifiSelectionActivity...");
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void OtaUpdateActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Turn off wifi — this causes any in-progress HTTP in the worker to fail quickly
  WiFi.disconnect(false);  // false = don't erase credentials, send disconnect frame
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);

  // Give the worker longer to exit — WiFi going down causes in-flight HTTP to
  // fail, but esp_https_ota abort + cleanup can take a few seconds.
  workerExitRequested.store(true);
  if (otaWorkerTaskHandle) {
    xTaskNotify(otaWorkerTaskHandle, 1, eIncrement);
    constexpr int workerExitTimeoutMs = 20000;
    constexpr int pollMs = 10;
    for (int waited = 0; !workerHasExited.load() && waited < workerExitTimeoutMs; waited += pollMs) {
      vTaskDelay(pdMS_TO_TICKS(pollMs));
    }
    if (!workerHasExited.load()) {
      vTaskDelete(otaWorkerTaskHandle);
    }
    otaWorkerTaskHandle = nullptr;
  }

  TaskShutdown::requestExit(exitTaskRequested, taskHasExited, displayTaskHandle);
}

void OtaUpdateActivity::displayTaskLoop() {
  while (!exitTaskRequested.load()) {
    if (updateRequired || updater.getRender()) {
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

void OtaUpdateActivity::render() {
  if (subActivity) {
    // Subactivity handles its own rendering
    return;
  }

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS) {
    const size_t processed = updater.getProcessedSize();
    const size_t total = updater.getTotalSize();
    LOG_DBG("OTA", "Update progress: %u / %u", (unsigned)processed, (unsigned)total);
    updaterProgress = (total > 0) ? static_cast<float>(processed) / static_cast<float>(total) : 0.0f;
    // Only update every 2% at the most
    if (total > 0 && static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) {
      return;
    }
    lastUpdaterPercentage = static_cast<int>(updaterProgress * 100);
  }

  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Update", true, EpdFontFamily::BOLD);

  if (state == CHECKING_FOR_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, 300, "Checking for update...", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == LOADING_FEATURE_STORE) {
    renderer.drawCenteredText(UI_10_FONT_ID, 300, "Loading feature store...", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SELECTING_FEATURE_STORE_BUNDLE) {
    const auto& entries = updater.getFeatureStoreEntries();
    const auto& entry = entries[selectedBundleIndex];
    const bool isInstalled = (String(SETTINGS.installedOtaBundle) == entry.id);

    // Section header + navigation counter
    renderer.drawText(UI_10_FONT_ID, 15, 55, "Feature Store", true, EpdFontFamily::BOLD);
    const String counter = String((int)(selectedBundleIndex + 1)) + " / " + String((int)entries.size());
    const int counterW = renderer.getTextWidth(UI_10_FONT_ID, counter.c_str());
    renderer.drawText(UI_10_FONT_ID, pageWidth - counterW - 15, 55, counter.c_str());

    // Card border
    const int cardX = 15;
    const int cardY = 78;
    const int cardW = pageWidth - 30;
    const int cardH = 510;
    renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 2, 8, true);

    int y = cardY + 24;
    const int textX = cardX + 14;
    const int innerW = cardW - 28;

    // Bundle name
    const auto nameStr = renderer.truncatedText(UI_12_FONT_ID, entry.displayName.c_str(), innerW, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, textX, y, nameStr.c_str(), true, EpdFontFamily::BOLD);
    y += 32;

    // Installed badge
    if (isInstalled) {
      renderer.drawText(UI_10_FONT_ID, textX, y, "* Installed *");
      y += 22;
    }

    // Compatibility warning (shown early so user sees it before selecting)
    if (!entry.compatible) {
      const auto warnStr =
          renderer.truncatedText(UI_10_FONT_ID, ("! " + entry.compatibilityError).c_str(), innerW, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, textX, y, warnStr.c_str(), true, EpdFontFamily::BOLD);
      y += 22;
    }

    // Version: add "v" prefix only for numeric-style versions (semver, date, commit count)
    const String& ver = entry.version;
    const String versionLabel = (!ver.isEmpty() && ver[0] >= '0' && ver[0] <= '9') ? "v" + ver : ver;
    renderer.drawText(UI_10_FONT_ID, textX, y, versionLabel.c_str());
    y += 26;

    // Divider
    renderer.drawLine(cardX + 10, y, cardX + cardW - 10, y);
    y += 15;

    // Feature flags: split on comma, strip "FEATURE_" prefix, replace '_' with ' '
    {
      String remaining = entry.featureFlags;
      int featureCount = 0;
      while (remaining.length() > 0 && featureCount < 12) {
        const int commaIdx = remaining.indexOf(',');
        String flag = (commaIdx >= 0) ? remaining.substring(0, commaIdx) : remaining;
        if (flag.startsWith("FEATURE_")) {
          flag = flag.substring(8);
        }
        flag.replace('_', ' ');
        if (flag.length() > 0) {
          renderer.drawText(UI_10_FONT_ID, textX + 6, y, ("• " + flag).c_str());
          y += 20;
          ++featureCount;
        }
        if (commaIdx < 0) break;
        remaining = remaining.substring(commaIdx + 1);
      }
    }

    // Binary size (when known)
    if (entry.binarySize > 0) {
      y += 8;
      renderer.drawText(UI_10_FONT_ID, textX, y, ("Size: " + String(entry.binarySize / 1024) + " KB").c_str());
    }

    const auto labels = mappedInput.mapLabels("Back", "Select", "Prev", "Next");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == WAITING_CONFIRMATION) {
    renderer.drawCenteredText(UI_10_FONT_ID, 200, "New update available!", true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, 20, 250, "Current Version: " CROSSPOINT_VERSION);
    renderer.drawText(UI_10_FONT_ID, 20, 270, ("New Version: " + updater.getLatestVersion()).c_str());
    if (usingFeatureStore) {
      renderer.drawText(UI_10_FONT_ID, 20, 290, ("Bundle: " + String(SETTINGS.selectedOtaBundle)).c_str());
    }
    if (updater.willFactoryResetOnInstall()) {
      renderer.drawCenteredText(UI_10_FONT_ID, 315, "Factory reset update selected.", true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, 340, "CrossPoint data will be erased after install.");
    }

    const auto labels = mappedInput.mapLabels("Cancel", "Update", "", "");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == UPDATE_IN_PROGRESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, 310, "Updating...", true, EpdFontFamily::BOLD);
    renderer.drawRect(20, 350, pageWidth - 40, 50);
    renderer.fillRect(24, 354, static_cast<int>(updaterProgress * static_cast<float>(pageWidth - 44)), 42);
    renderer.drawCenteredText(UI_10_FONT_ID, 420,
                              (std::to_string(static_cast<int>(updaterProgress * 100)) + "%").c_str());
    renderer.drawCenteredText(
        UI_10_FONT_ID, 440,
        (std::to_string(updater.getProcessedSize()) + " / " + std::to_string(updater.getTotalSize())).c_str());
    renderer.displayBuffer();
    return;
  }

  if (state == NO_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, 300, "No update available", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, 300, "Update failed", true, EpdFontFamily::BOLD);
    const String& error = updater.getLastError();
    if (error.length() > 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, 330, error.c_str());
    }
    renderer.displayBuffer();
    return;
  }

  if (state == FINISHED) {
    renderer.drawCenteredText(UI_10_FONT_ID, 300, "Update complete", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 350, "Press and hold power button to turn back on");
    renderer.displayBuffer();
    state = SHUTTING_DOWN;
    return;
  }
}

void OtaUpdateActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (state == SELECTING_FEATURE_STORE_BUNDLE) {
    const auto& entries = updater.getFeatureStoreEntries();

    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (selectedBundleIndex > 0) {
        selectedBundleIndex--;
        updateRequired = true;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (selectedBundleIndex < entries.size() - 1) {
        selectedBundleIndex++;
        updateRequired = true;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (updater.selectFeatureStoreBundleByIndex(selectedBundleIndex)) {
        LOG_INF("OTA", "Selected bundle: %s", entries[selectedBundleIndex].id.c_str());
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = CHECKING_FOR_UPDATE;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;
        dispatchWorker(OtaWorkerCmd::CHECK_UPDATE);
      } else {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = FAILED;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      usingFeatureStore = false;
      goBack();
    }

    return;
  }

  if (state == WAITING_CONFIRMATION) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      LOG_INF("OTA", "User confirmed update, dispatching install to worker");
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = UPDATE_IN_PROGRESS;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      dispatchWorker(OtaWorkerCmd::INSTALL_UPDATE);
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      goBack();
    }

    return;
  }

  if (state == FAILED) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == NO_UPDATE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
