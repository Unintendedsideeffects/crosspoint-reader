#include "OtaUpdater.h"

#include <ArduinoJson.h>
#include <FeatureManifest.h>
#include <HalStorage.h>
#include <Logging.h>

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
constexpr char featureStoreCatalogUrl[] =
    "https://raw.githubusercontent.com/Unintendedsideeffects/crosspoint-reader/master/docs/ota/"
    "feature-store-catalog.json";
constexpr char expectedBoard[] = "esp32s3";
constexpr char crosspointDataDir[] = "/.crosspoint";
constexpr char factoryResetMarkerFile[] = "/.factory-reset-pending";

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

/* This is buffer and size holder to keep upcoming data from latestReleaseUrl */
char* local_buf;
int output_len;

/*
 * When esp_crt_bundle.h included, it is pointing wrong header file
 * which is something under WifiClientSecure because of our framework based on arduno platform.
 * To manage this obstacle, don't include anything, just extern and it will point correct one.
 */
extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

esp_err_t http_client_set_header_cb(esp_http_client_handle_t http_client) {
  return esp_http_client_set_header(http_client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
}

esp_err_t event_handler(esp_http_client_event_t* event) {
  /* We do interested in only HTTP_EVENT_ON_DATA event only */
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

  if (!esp_http_client_is_chunked_response(event->client)) {
    int content_len = esp_http_client_get_content_length(event->client);
    int copy_len = 0;

    if (local_buf == NULL) {
      /* local_buf life span is tracked by caller checkForUpdate */
      local_buf = static_cast<char*>(calloc(content_len + 1, sizeof(char)));
      output_len = 0;
      if (local_buf == NULL) {
        LOG_ERR("OTA", "HTTP Client Out of Memory Failed, Allocation %d", content_len);
        return ESP_ERR_NO_MEM;
      }
    }
    copy_len = min(event->data_len, (content_len - output_len));
    if (copy_len) {
      memcpy(local_buf + output_len, event->data, copy_len);
    }
    output_len += copy_len;
  } else {
    /* Code might be hits here, It happened once (for version checking) but I need more logs to handle that */
    int chunked_len;
    esp_http_client_get_chunk_length(event->client, &chunked_len);
    LOG_DBG("OTA", "esp_http_client_is_chunked_response failed, chunked_len: %d", chunked_len);
  }

  const int data_len = event->data_len;
  char* new_buf = static_cast<char*>(realloc(local_buf, output_len + data_len + 1));
  if (new_buf == NULL) {
    LOG_ERR("OTA", "HTTP Client Out of Memory Failed, Allocation %d", data_len);
    return ESP_ERR_NO_MEM;
  }

  local_buf = new_buf;
  memcpy(local_buf + output_len, event->data, data_len);
  output_len += data_len;
  local_buf[output_len] = '\0';
  return ESP_OK;
} /* event_handler */

/**
 * Fetch JSON from a URL into ArduinoJson doc, using the shared local_buf/output_len globals.
 * Returns OtaUpdater::OK on success, or an error code.
 */
OtaUpdater::OtaUpdaterError fetchReleaseJson(const char* url, JsonDocument& doc, const JsonDocument& filter) {
  local_buf = nullptr;
  output_len = 0;

  esp_http_client_config_t client_config = {
      .url = url,
      .event_handler = event_handler,
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  struct localBufCleaner {
    char** bufPtr;
    ~localBufCleaner() {
      if (*bufPtr) {
        free(*bufPtr);
        *bufPtr = NULL;
      }
    }
  } cleaner = {&local_buf};

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

  const DeserializationError error = deserializeJson(doc, local_buf, DeserializationOption::Filter(filter));
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
    return JSON_PARSE_ERROR;
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

  latestVersion = doc["tag_name"].as<std::string>();

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

  local_buf = nullptr;
  output_len = 0;

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

  return true;
}

const String& OtaUpdater::getLastError() const { return lastError; }

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty()) {
    return false;
  }

  if (latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  int currentMajor = 0;
  int currentMinor = 0;
  int currentPatch = 0;
  int latestMajor = 0;
  int latestMinor = 0;
  int latestPatch = 0;

  const auto currentVersion = std::string(CROSSPOINT_VERSION);

  const bool latestParsed = parseSemver(latestVersion, latestMajor, latestMinor, latestPatch);
  const bool currentParsed = parseSemver(currentVersion, currentMajor, currentMinor, currentPatch);
  if (!latestParsed || !currentParsed) {
    return latestVersion != currentVersion;
  }

  /*
   * Compare major versions.
   * If they differ, return true if latest major version greater than current major version
   * otherwise return false.
   */
  if (latestMajor != currentMajor) return latestMajor > currentMajor;

  /*
   * Compare minor versions.
   * If they differ, return true if latest minor version greater than current minor version
   * otherwise return false.
   */
  if (latestMinor != currentMinor) return latestMinor > currentMinor;

  /*
   * Check patch versions.
   */
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  // If we reach here, it means all segments are equal.
  // One final check, if we're on an RC build (contains "-rc"), we should consider the latest version as newer even if
  // the segments are equal, since RC builds are pre-release versions.
  if (strstr(currentVersion.c_str(), "-rc") != nullptr) {
    return true;
  }

  return false;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate() {
  if (!isUpdateNewer()) {
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
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  esp_https_ota_config_t ota_config = {
      .http_config = &client_config,
      .http_client_init_cb = http_client_set_header_cb,
  };

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_err = esp_https_ota_begin(&ota_config, &ota_handle);
  if (esp_err != ESP_OK) {
    LOG_DBG("OTA", "HTTP OTA Begin Failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  do {
    esp_err = esp_https_ota_perform(ota_handle);
    processedSize = esp_https_ota_get_image_len_read(ota_handle);
    /* Sent signal to  OtaUpdateActivity */
    render = true;
    vTaskDelay(10 / portTICK_PERIOD_MS);
  } while (esp_err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_perform Failed: %s", esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return HTTP_ERROR;
  }

  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    LOG_ERR("OTA", "esp_https_ota_is_complete_data_received Failed: %s", esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_https_ota_finish(ota_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_finish Failed: %s", esp_err_to_name(esp_err));
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
  }

  return OK;
}
