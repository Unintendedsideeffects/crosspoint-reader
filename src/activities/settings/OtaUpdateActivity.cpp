#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
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

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  exitActivity();

  if (!success) {
    Serial.printf("[%lu] [OTA] WiFi connection failed, exiting\n", millis());
    goBack();
    return;
  }

  Serial.printf("[%lu] [OTA] WiFi connected, loading feature store catalog\n", millis());

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = LOADING_FEATURE_STORE;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
  vTaskDelay(10 / portTICK_PERIOD_MS);

  if (updater.loadFeatureStoreCatalog() && updater.hasFeatureStoreCatalog()) {
    Serial.printf("[%lu] [OTA] Feature store catalog loaded, %d bundles\n", millis(),
                  updater.getFeatureStoreEntries().size());
    selectedBundleIndex = 0;
    usingFeatureStore = true;
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = SELECTING_FEATURE_STORE_BUNDLE;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  // Fall through to channel-based OTA
  Serial.printf("[%lu] [OTA] Feature store unavailable, falling back to channel OTA\n", millis());
  usingFeatureStore = false;

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = CHECKING_FOR_UPDATE;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
  vTaskDelay(10 / portTICK_PERIOD_MS);
  const auto res = updater.checkForUpdate();
  if (res != OtaUpdater::OK) {
    Serial.printf("[%lu] [OTA] Update check failed: %d\n", millis(), res);
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = FAILED;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  if (!updater.isUpdateNewer()) {
    Serial.printf("[%lu] [OTA] No new update available\n", millis());
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    state = NO_UPDATE;
    xSemaphoreGive(renderingMutex);
    updateRequired = true;
    return;
  }

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  state = WAITING_CONFIRMATION;
  xSemaphoreGive(renderingMutex);
  updateRequired = true;
}

void OtaUpdateActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  exitTaskRequested.store(false);
  taskHasExited.store(false);

  xTaskCreate(&OtaUpdateActivity::taskTrampoline, "OtaUpdateActivityTask",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );

  // Turn on WiFi immediately
  Serial.printf("[%lu] [OTA] Turning on WiFi...\n", millis());
  WiFi.mode(WIFI_STA);

  // Launch WiFi selection subactivity
  Serial.printf("[%lu] [OTA] Launching WifiSelectionActivity...\n", millis());
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void OtaUpdateActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Turn off wifi
  WiFi.disconnect(false);  // false = don't erase credentials, send disconnect frame
  delay(100);              // Allow disconnect frame to be sent
  WiFi.mode(WIFI_OFF);
  delay(100);  // Allow WiFi hardware to fully power down

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
    Serial.printf("[%lu] [OTA] Update progress: %d / %d\n", millis(), updater.getProcessedSize(),
                  updater.getTotalSize());
    updaterProgress = static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize());
    // Only update every 2% at the most
    if (static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) {
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
    renderer.drawCenteredText(UI_10_FONT_ID, 60, "Feature Store", true, EpdFontFamily::BOLD);

    const auto& entries = updater.getFeatureStoreEntries();
    int yPos = 100;
    const int lineHeight = 22;
    const int maxVisible = 8;
    const size_t startIdx = selectedBundleIndex >= maxVisible ? selectedBundleIndex - maxVisible + 1 : 0;

    for (size_t i = startIdx; i < entries.size() && i < startIdx + maxVisible; i++) {
      const auto& entry = entries[i];
      const bool selected = (i == selectedBundleIndex);
      const char* marker = selected ? "> " : "  ";

      String line = String(marker) + entry.displayName;
      if (!entry.compatible) {
        line += " [!]";
      }
      renderer.drawText(UI_10_FONT_ID, 10, yPos, line.c_str(), selected, EpdFontFamily::BOLD);
      yPos += lineHeight;

      if (selected) {
        // Show details for selected bundle
        String details = "  ID: " + entry.id + "  v" + entry.version;
        renderer.drawText(UI_10_FONT_ID, 10, yPos, details.c_str());
        yPos += lineHeight;

        if (entry.binarySize > 0) {
          String sizeStr = "  Size: " + String(entry.binarySize / 1024) + " KB";
          renderer.drawText(UI_10_FONT_ID, 10, yPos, sizeStr.c_str());
          yPos += lineHeight;
        }

        String features = "  Features: " + entry.featureFlags;
        renderer.drawText(UI_10_FONT_ID, 10, yPos, features.c_str());
        yPos += lineHeight;

        if (!entry.compatible) {
          renderer.drawText(UI_10_FONT_ID, 10, yPos, ("  Warning: " + entry.compatibilityError).c_str());
          yPos += lineHeight;
        }
      }
    }

    const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
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
        Serial.printf("[%lu] [OTA] Selected bundle: %s\n", millis(), entries[selectedBundleIndex].id.c_str());
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = CHECKING_FOR_UPDATE;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;
        vTaskDelay(10 / portTICK_PERIOD_MS);

        const auto res = updater.checkForUpdate();
        if (res != OtaUpdater::OK) {
          xSemaphoreTake(renderingMutex, portMAX_DELAY);
          state = FAILED;
          xSemaphoreGive(renderingMutex);
          updateRequired = true;
          return;
        }

        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = WAITING_CONFIRMATION;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;
      } else {
        // Incompatible bundle selected
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
      Serial.printf("[%lu] [OTA] New update available, starting download...\n", millis());
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = UPDATE_IN_PROGRESS;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
      vTaskDelay(10 / portTICK_PERIOD_MS);
      const auto res = updater.installUpdate();

      if (res != OtaUpdater::OK) {
        Serial.printf("[%lu] [OTA] Update failed: %d\n", millis(), res);
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        state = FAILED;
        xSemaphoreGive(renderingMutex);
        updateRequired = true;
        return;
      }

      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      state = FINISHED;
      xSemaphoreGive(renderingMutex);
      updateRequired = true;
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
