#include "OtaUpdater.h"

#include <ArduinoJson.h>
#include <FeatureManifest.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "SpiBusMutex.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"

namespace {
constexpr char releaseChannelUrl[] =
    "https://api.github.com/repos/Unintendedsideeffects/crosspoint-reader/releases/tags/release";
constexpr char nightlyChannelUrl[] =
    "https://api.github.com/repos/Unintendedsideeffects/crosspoint-reader/releases/tags/nightly";
constexpr char latestChannelUrl[] =
    "https://api.github.com/repos/Unintendedsideeffects/crosspoint-reader/releases/tags/latest";
constexpr char resetChannelUrl[] =
    "https://api.github.com/repos/Unintendedsideeffects/crosspoint-reader/releases/tags/reset";
// Override at build time via -DFEATURE_STORE_CATALOG_URL='"..."' in platformio.ini build_flags.
#ifndef FEATURE_STORE_CATALOG_URL
#define FEATURE_STORE_CATALOG_URL                                                                  \
  "https://raw.githubusercontent.com/Unintendedsideeffects/crosspoint-reader/fork-drift/docs/ota/" \
  "feature-store-catalog.json"
#endif
constexpr char featureStoreCatalogUrl[] = FEATURE_STORE_CATALOG_URL;
constexpr char expectedBoard[] = "esp32c3";
constexpr char crosspointDataDir[] = "/.crosspoint";
constexpr char factoryResetMarkerFile[] = "/.factory-reset-pending";
constexpr uint32_t otaNoProgressTimeoutMs = 45000;

bool markFactoryResetPending() {
  FsFile markerFile;
  {
    SpiBusMutex::Guard guard;
    if (!Storage.openFileForWrite("OTA", factoryResetMarkerFile, markerFile)) {
      LOG_ERR("OTA", "Failed to create factory reset marker: %s", factoryResetMarkerFile);
      return false;
    }
    static constexpr uint8_t markerByte = 1;
    markerFile.write(&markerByte, sizeof(markerByte));
    markerFile.close();
  }

  LOG_INF("OTA", "Factory reset marker created: %s", factoryResetMarkerFile);
  return true;
}

bool wipeCrossPointData() {
  bool removed = true;
  bool ensuredDir = true;
  {
    SpiBusMutex::Guard guard;
    if (Storage.exists(crosspointDataDir)) {
      removed = Storage.removeDir(crosspointDataDir);
    }
    if (!Storage.exists(crosspointDataDir)) {
      ensuredDir = Storage.mkdir(crosspointDataDir);
    }
  }

  if (!removed || !ensuredDir) {
    LOG_ERR("OTA", "Factory reset wipe failed (removed=%d, ensuredDir=%d)", removed, ensuredDir);
    return false;
  }

  LOG_INF("OTA", "Factory reset wipe completed");
  return true;
}

bool parseSemver(const std::string& version, int& major, int& minor, int& patch) {
  const char* versionStr = version.c_str();
  if (versionStr[0] == 'v' || versionStr[0] == 'V') {
    versionStr += 1;
  }
  return sscanf(versionStr, "%d.%d.%d", &major, &minor, &patch) == 3;
}

// "12345-dev" → commit count format. Returns true and sets count on match.
bool parseCommitDev(const std::string& v, unsigned long& count) {
  if (v.size() < 5 || v.compare(v.size() - 4, 4, "-dev") != 0) return false;
  const auto numStr = v.substr(0, v.size() - 4);
  if (numStr.empty()) return false;
  for (const char c : numStr) {
    if (c < '0' || c > '9') return false;
  }
  return sscanf(numStr.c_str(), "%lu", &count) == 1;
}

// "20240218" → YYYYMMDD date format. Returns true and sets date on match.
bool parseBuildDate(const std::string& v, unsigned long& date) {
  if (v.size() != 8) return false;
  for (const char c : v) {
    if (c < '0' || c > '9') return false;
  }
  if (sscanf(v.c_str(), "%lu", &date) != 1) return false;
  // Sanity check: must look like a real date after year 2020
  return date >= 20200101UL && date <= 29991231UL;
}

/*
 * When esp_crt_bundle.h included, it is pointing wrong header file
 * which is something under WifiClientSecure because of our framework based on arduno platform.
 * To manage this obstacle, don't include anything, just extern and it will point correct one.
 */
extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

// Buffer accumulated by the HTTP event handler during a JSON fetch.
// Passed via user_data so each fetch call has its own isolated state.
struct HttpBuf {
  char* data = nullptr;
  size_t len = 0;
};

esp_err_t http_client_set_header_cb(esp_http_client_handle_t http_client) {
  return esp_http_client_set_header(http_client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
}

esp_err_t event_handler(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

  // Append this chunk to the caller-supplied buffer.
  // realloc(NULL, n) behaves as malloc(n), so this handles the first call correctly.
  auto* buf = static_cast<HttpBuf*>(event->user_data);
  const size_t data_len = static_cast<size_t>(event->data_len);
  char* new_data = static_cast<char*>(realloc(buf->data, buf->len + data_len + 1));
  if (new_data == NULL) {
    LOG_ERR("OTA", "Failed to allocate HTTP response buffer (%zu bytes)", buf->len + data_len + 1);
    return ESP_ERR_NO_MEM;
  }

  buf->data = new_data;
  memcpy(buf->data + buf->len, event->data, data_len);
  buf->len += data_len;
  buf->data[buf->len] = '\0';
  return ESP_OK;
}

/**
 * Fetch JSON from a URL into ArduinoJson doc.
 * Returns OtaUpdater::OK on success, or an error code.
 */
OtaUpdater::OtaUpdaterError fetchReleaseJson(const char* url, JsonDocument& doc, const JsonDocument& filter) {
  HttpBuf httpBuf;

  esp_http_client_config_t client_config = {
      .url = url,
      .event_handler = event_handler,
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .user_data = &httpBuf,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  struct localBufCleaner {
    HttpBuf* buf;
    ~localBufCleaner() {
      if (buf->data) {
        free(buf->data);
        buf->data = nullptr;
      }
    }
  } cleaner = {&httpBuf};

  esp_http_client_handle_t client_handle = esp_http_client_init(&client_config);
  if (!client_handle) {
    LOG_ERR("OTA", "HTTP Client Handle Failed");
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }

  esp_err_t esp_err = esp_http_client_set_header(client_handle, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_set_header Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_perform(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_perform Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return OtaUpdater::HTTP_ERROR;
  }

  esp_err = esp_http_client_cleanup(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_cleanup Failed : %s", esp_err_to_name(esp_err));
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }

  const DeserializationError error = deserializeJson(doc, httpBuf.data, DeserializationOption::Filter(filter));
  if (error) {
    LOG_ERR("OTA", "JSON parse failed: %s", error.c_str());
    return OtaUpdater::JSON_PARSE_ERROR;
  }

  return OtaUpdater::OK;
}

size_t getMaxOtaPartitionSize() {
  const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
  if (partition) {
    return partition->size;
  }
  return 0;
}

} /* namespace */

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  factoryResetOnInstall = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = 0;
  totalSize = 0;

  // If a feature store bundle was selected, use its URL directly
  if (!selectedBundleId.isEmpty()) {
    for (const auto& entry : featureStoreEntries) {
      if (entry.id == selectedBundleId) {
        otaUrl = entry.downloadUrl.c_str();
        otaSize = entry.binarySize;
        totalSize = entry.binarySize;
        latestVersion = entry.version.c_str();
        updateAvailable = true;
        LOG_DBG("OTA", "Using feature store bundle: %s", selectedBundleId.c_str());
        return OK;
      }
    }
    lastError = BUNDLE_UNAVAILABLE_ERROR;
    return INTERNAL_UPDATE_ERROR;
  }

  // Fall back to channel-based OTA
  const char* releaseUrl = releaseChannelUrl;
  if (SETTINGS.releaseChannel == CrossPointSettings::RELEASE_NIGHTLY) {
    releaseUrl = nightlyChannelUrl;
  } else if (SETTINGS.releaseChannel == CrossPointSettings::RELEASE_LATEST_SUCCESSFUL) {
    releaseUrl = latestChannelUrl;
  } else if (SETTINGS.releaseChannel == CrossPointSettings::RELEASE_LATEST_SUCCESSFUL_FACTORY_RESET) {
    releaseUrl = resetChannelUrl;
    factoryResetOnInstall = true;
  }

  JsonDocument filter;
  JsonDocument doc;
  filter["tag_name"] = true;
  filter["name"] = true;  // release title — CI sets this to the build version string
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  filter["assets"][0]["size"] = true;

  const auto res = fetchReleaseJson(releaseUrl, doc, filter);
  if (res != OK) {
    return res;
  }

  if (!doc["tag_name"].is<std::string>()) {
    LOG_ERR("OTA", "No tag_name found");
    return JSON_PARSE_ERROR;
  }

  if (!doc["assets"].is<JsonArray>()) {
    LOG_ERR("OTA", "No assets found");
    return JSON_PARSE_ERROR;
  }

  // Use the release title (name) as the build version identifier — it carries
  // meaningful version strings like "12345-dev", "20240218", or "1.0.0".
  // Fall back to tag_name for older releases that predate this convention.
  const std::string releaseName = doc["name"] | "";
  latestVersion = releaseName.empty() ? doc["tag_name"].as<std::string>() : releaseName;

  for (int i = 0; i < doc["assets"].size(); i++) {
    if (doc["assets"][i]["name"] == "firmware.bin") {
      otaUrl = doc["assets"][i]["browser_download_url"].as<std::string>();
      otaSize = doc["assets"][i]["size"].as<size_t>();
      totalSize = otaSize;
      updateAvailable = true;
      break;
    }
  }

  if (!updateAvailable) {
    LOG_ERR("OTA", "No firmware.bin asset found");
    return NO_UPDATE;
  }

  LOG_DBG("OTA", "Found update: %s", latestVersion.c_str());
  return OK;
}

bool OtaUpdater::loadFeatureStoreCatalog() {
  featureStoreEntries.clear();
  lastError.clear();

  JsonDocument filter;
  JsonDocument doc;
  filter["bundles"][0]["id"] = true;
  filter["bundles"][0]["displayName"] = true;
  filter["bundles"][0]["version"] = true;
  filter["bundles"][0]["board"] = true;
  filter["bundles"][0]["featureFlags"] = true;
  filter["bundles"][0]["downloadUrl"] = true;
  filter["bundles"][0]["checksum"] = true;
  filter["bundles"][0]["binarySize"] = true;

  const auto res = fetchReleaseJson(featureStoreCatalogUrl, doc, filter);
  if (res != OK) {
    lastError = CATALOG_UNAVAILABLE_ERROR;
    return false;
  }

  if (!doc["bundles"].is<JsonArray>()) {
    lastError = CATALOG_UNAVAILABLE_ERROR;
    return false;
  }

  const size_t maxPartitionSize = getMaxOtaPartitionSize();

  for (const auto& bundle : doc["bundles"].as<JsonArray>()) {
    FeatureStoreEntry entry;
    entry.id = bundle["id"].as<const char*>();
    entry.displayName = bundle["displayName"].as<const char*>();
    entry.version = bundle["version"].as<const char*>();
    entry.featureFlags = bundle["featureFlags"].as<const char*>();
    entry.downloadUrl = bundle["downloadUrl"].as<const char*>();
    entry.checksum = bundle["checksum"] | "";
    entry.binarySize = bundle["binarySize"] | 0;

    const char* board = bundle["board"] | "";
    if (strcmp(board, expectedBoard) != 0) {
      entry.compatible = false;
      entry.compatibilityError = "Requires board: " + String(board);
    } else if (entry.binarySize > 0 && maxPartitionSize > 0 && entry.binarySize > maxPartitionSize) {
      entry.compatible = false;
      entry.compatibilityError = "Binary too large for OTA partition";
    }

    featureStoreEntries.push_back(entry);
  }

  return !featureStoreEntries.empty();
}

bool OtaUpdater::hasFeatureStoreCatalog() const { return !featureStoreEntries.empty(); }

const std::vector<OtaUpdater::FeatureStoreEntry>& OtaUpdater::getFeatureStoreEntries() const {
  return featureStoreEntries;
}

bool OtaUpdater::selectFeatureStoreBundleByIndex(size_t index) {
  if (index >= featureStoreEntries.size()) {
    return false;
  }

  const auto& entry = featureStoreEntries[index];
  if (!entry.compatible) {
    lastError = INCOMPATIBLE_BUNDLE_ERROR;
    return false;
  }

  selectedBundleId = entry.id;
  selectedFeatureFlags = entry.featureFlags;
  selectedChecksum = entry.checksum;

  // Persist selection
  strncpy(SETTINGS.selectedOtaBundle, entry.id.c_str(), sizeof(SETTINGS.selectedOtaBundle) - 1);
  SETTINGS.selectedOtaBundle[sizeof(SETTINGS.selectedOtaBundle) - 1] = '\0';
  SETTINGS.saveToFile();

  return true;
}

const String& OtaUpdater::getLastError() const { return lastError; }

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty()) {
    return false;
  }

  // Identical strings → same build, nothing to do.
  if (latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  const std::string currentV(CROSSPOINT_VERSION);
  const std::string& latestV = latestVersion;

  // --- Commit-dev format: "12345-dev" ---
  unsigned long latestCommit = 0, currentCommit = 0;
  const bool latestIsCommitDev = parseCommitDev(latestV, latestCommit);
  const bool currentIsCommitDev = parseCommitDev(currentV, currentCommit);
  if (latestIsCommitDev && currentIsCommitDev) {
    return latestCommit > currentCommit;
  }

  // --- Date format: "YYYYMMDD" ---
  unsigned long latestDate = 0, currentDate = 0;
  const bool latestIsDate = parseBuildDate(latestV, latestDate);
  const bool currentIsDate = parseBuildDate(currentV, currentDate);
  if (latestIsDate && currentIsDate) {
    return latestDate > currentDate;
  }

  // --- Semver: "1.2.3" or "v1.2.3" ---
  int latestMaj = 0, latestMin = 0, latestPat = 0;
  int currentMaj = 0, currentMin = 0, currentPat = 0;
  const bool latestIsSemver = parseSemver(latestV, latestMaj, latestMin, latestPat);
  const bool currentIsSemver = parseSemver(currentV, currentMaj, currentMin, currentPat);
  if (latestIsSemver && currentIsSemver) {
    if (latestMaj != currentMaj) return latestMaj > currentMaj;
    if (latestMin != currentMin) return latestMin > currentMin;
    if (latestPat != currentPat) return latestPat > currentPat;
    // Equal semver segments: still offer the update if currently on a pre-release
    // (e.g. RC build getting the final stable, or dev/custom suffix builds).
    return strstr(currentV.c_str(), "-") != nullptr;
  }

  // --- Cross-format or unrecognised tokens (e.g. feature-store "latest"/"nightly") ---
  // Formats differ → device is on one version scheme, server returned another.
  // This is a deliberate channel switch (e.g. semver stable → date-format nightly).
  // Version strings are not comparable as scalars, so we allow the install.
  // If gating is required, the calling UI layer should confirm with the user before
  // invoking installUpdate() when isUpdateNewer() returns true across channel types.
  return true;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate() {
  lastError.clear();
  processedSize = 0;
  render = false;

  if (!isUpdateNewer()) {
    lastError = "No newer update available";
    return UPDATE_OLDER_ERROR;
  }

  esp_https_ota_handle_t ota_handle = NULL;
  esp_err_t esp_err;
  /* Signal for OtaUpdateActivity */
  render = false;

  esp_http_client_config_t client_config = {
      .url = otaUrl.c_str(),
      .timeout_ms = 15000,
      /* Default HTTP client buffer size 512 byte only
       * not sufficent to handle URL redirection cases or
       * parsing of large HTTP headers.
       */
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  esp_https_ota_config_t ota_config = {
      .http_config = &client_config,
      .http_client_init_cb = http_client_set_header_cb,
  };

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);
  struct WifiPsRestore {
    ~WifiPsRestore() { esp_wifi_set_ps(WIFI_PS_MIN_MODEM); }
  } wifiPsRestore;

  auto abortOta = [&](const OtaUpdaterError code, const String& message) {
    lastError = message;
    if (ota_handle != NULL) {
      const esp_err_t abortErr = esp_https_ota_abort(ota_handle);
      if (abortErr != ESP_OK) {
        LOG_WRN("OTA", "esp_https_ota_abort failed: %s", esp_err_to_name(abortErr));
      }
      ota_handle = NULL;
    }
    return code;
  };

  esp_err = esp_https_ota_begin(&ota_config, &ota_handle);
  if (esp_err != ESP_OK) {
    LOG_DBG("OTA", "HTTP OTA Begin Failed: %s", esp_err_to_name(esp_err));
    lastError = "Failed to start OTA session";
    return INTERNAL_UPDATE_ERROR;
  }

  size_t lastProgressBytes = 0;
  unsigned long lastProgressAt = millis();

  do {
    if (WiFi.status() != WL_CONNECTED) {
      LOG_ERR("OTA", "WiFi disconnected during OTA");
      return abortOta(HTTP_ERROR, "WiFi disconnected during update");
    }

    esp_err = esp_https_ota_perform(ota_handle);
    processedSize = esp_https_ota_get_image_len_read(ota_handle);
    if (processedSize > lastProgressBytes) {
      lastProgressBytes = processedSize;
      lastProgressAt = millis();
    } else if (esp_err == ESP_ERR_HTTPS_OTA_IN_PROGRESS && (millis() - lastProgressAt) > otaNoProgressTimeoutMs) {
      LOG_ERR("OTA", "OTA stalled for >%lu ms without progress", static_cast<unsigned long>(otaNoProgressTimeoutMs));
      return abortOta(HTTP_ERROR, "Update stalled; check network and retry");
    }

    /* Sent signal to  OtaUpdateActivity */
    render = true;
    vTaskDelay(10 / portTICK_PERIOD_MS);
  } while (esp_err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_perform Failed: %s", esp_err_to_name(esp_err));
    return abortOta(HTTP_ERROR, "Update download failed");
  }

  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    LOG_ERR("OTA", "OTA image incomplete; aborting install");
    return abortOta(INTERNAL_UPDATE_ERROR, "Incomplete update data received");
  }

  esp_err = esp_https_ota_finish(ota_handle);
  ota_handle = NULL;
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_finish Failed: %s", esp_err_to_name(esp_err));
    lastError = "Failed to finalize update";
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");

  // Persist installed bundle info
  if (!selectedBundleId.isEmpty()) {
    strncpy(SETTINGS.installedOtaBundle, selectedBundleId.c_str(), sizeof(SETTINGS.installedOtaBundle) - 1);
    SETTINGS.installedOtaBundle[sizeof(SETTINGS.installedOtaBundle) - 1] = '\0';
    strncpy(SETTINGS.installedOtaFeatureFlags, selectedFeatureFlags.c_str(),
            sizeof(SETTINGS.installedOtaFeatureFlags) - 1);
    SETTINGS.installedOtaFeatureFlags[sizeof(SETTINGS.installedOtaFeatureFlags) - 1] = '\0';
    SETTINGS.saveToFile();
  }

  // Write the deferred factory-reset sentinel so boot picks it up on next cold start.
  // The flash itself already succeeded; log a warning if the marker write fails rather than
  // returning an error that would prevent the activity from signalling reboot to the user.
  if (factoryResetOnInstall && !markFactoryResetPending()) {
    LOG_WRN("OTA", "Factory reset marker write failed — data wipe will NOT occur on next boot");
  }

  return OK;
}
