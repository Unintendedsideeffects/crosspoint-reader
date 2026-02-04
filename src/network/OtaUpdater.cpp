#include "OtaUpdater.h"

#include <ArduinoJson.h>

#include "CrossPointSettings.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_wifi.h"

namespace {
constexpr char latestReleaseUrl[] =
    "https://api.github.com/repos/Unintendedsideeffects/crosspoint-reader/releases/latest";
constexpr char releasesListUrl[] =
    "https://api.github.com/repos/Unintendedsideeffects/crosspoint-reader/releases?per_page=5";
constexpr char ciLatestReleaseUrl[] =
    "https://api.github.com/repos/Unintendedsideeffects/crosspoint-reader/releases/tags/ci-latest";

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
  if (event->data_len <= 0) {
    return ESP_OK;
  }

  const int data_len = event->data_len;
  char* new_buf = static_cast<char*>(realloc(local_buf, output_len + data_len + 1));
  if (new_buf == NULL) {
    Serial.printf("[%lu] [OTA] HTTP Client Out of Memory Failed, Allocation %d\n", millis(), data_len);
    return ESP_ERR_NO_MEM;
  }

  local_buf = new_buf;
  memcpy(local_buf + output_len, event->data, data_len);
  output_len += data_len;
  local_buf[output_len] = '\0';
  return ESP_OK;
} /* event_handler */
} /* namespace */

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = 0;
  totalSize = 0;
  local_buf = nullptr;
  output_len = 0;

  JsonDocument filter;
  esp_err_t esp_err;
  JsonDocument doc;

  const bool useReleaseList = (SETTINGS.releaseChannel == CrossPointSettings::RELEASE_NIGHTLY);
  const char* releaseUrl = latestReleaseUrl;
  if (SETTINGS.releaseChannel == CrossPointSettings::RELEASE_NIGHTLY) {
    releaseUrl = releasesListUrl;
  } else if (SETTINGS.releaseChannel == CrossPointSettings::RELEASE_LATEST_SUCCESSFUL) {
    releaseUrl = ciLatestReleaseUrl;
  }

  esp_http_client_config_t client_config = {
      .url = releaseUrl,
      .event_handler = event_handler,
      /* Default HTTP client buffer size 512 byte only */
      .buffer_size = 8192,
      .buffer_size_tx = 8192,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  /* To track life time of local_buf, dtor will be called on exit from that function */
  struct localBufCleaner {
    char** bufPtr;
    ~localBufCleaner() {
      if (*bufPtr) {
        free(*bufPtr);
        *bufPtr = NULL;
      }
    }
  } localBufCleaner = {&local_buf};

  esp_http_client_handle_t client_handle = esp_http_client_init(&client_config);
  if (!client_handle) {
    Serial.printf("[%lu] [OTA] HTTP Client Handle Failed\n", millis());
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_set_header(client_handle, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (esp_err != ESP_OK) {
    Serial.printf("[%lu] [OTA] esp_http_client_set_header Failed : %s\n", millis(), esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_perform(client_handle);
  if (esp_err != ESP_OK) {
    Serial.printf("[%lu] [OTA] esp_http_client_perform Failed : %s\n", millis(), esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return HTTP_ERROR;
  }

  /* esp_http_client_close will be called inside cleanup as well*/
  esp_err = esp_http_client_cleanup(client_handle);
  if (esp_err != ESP_OK) {
    Serial.printf("[%lu] [OTA] esp_http_client_cleanupp Failed : %s\n", millis(), esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  if (useReleaseList) {
    filter[0]["tag_name"] = true;
    filter[0]["prerelease"] = true;
    filter[0]["draft"] = true;
    filter[0]["assets"][0]["name"] = true;
    filter[0]["assets"][0]["browser_download_url"] = true;
    filter[0]["assets"][0]["size"] = true;
  } else {
    filter["tag_name"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    filter["assets"][0]["size"] = true;
  }
  const DeserializationError error = deserializeJson(doc, local_buf, DeserializationOption::Filter(filter));
  if (error) {
    Serial.printf("[%lu] [OTA] JSON parse failed: %s\n", millis(), error.c_str());
    return JSON_PARSE_ERROR;
  }

  JsonVariant release = doc.as<JsonVariant>();
  if (useReleaseList) {
    if (!doc.is<JsonArray>() || doc.size() == 0) {
      Serial.printf("[%lu] [OTA] No releases found\n", millis());
      return JSON_PARSE_ERROR;
    }
    bool releaseFound = false;
    for (const auto& candidate : doc.as<JsonArray>()) {
      if (candidate["draft"].is<bool>() && candidate["draft"].as<bool>()) {
        continue;
      }
      if (!candidate["prerelease"].is<bool>() || !candidate["prerelease"].as<bool>()) {
        continue;
      }
      if (candidate["tag_name"] == "ci-latest") {
        continue;
      }
      release = candidate;
      releaseFound = true;
      break;
    }
    if (!releaseFound) {
      Serial.printf("[%lu] [OTA] No nightly releases found\n", millis());
      return JSON_PARSE_ERROR;
    }
  }

  if (!release["tag_name"].is<std::string>()) {
    Serial.printf("[%lu] [OTA] No tag_name found\n", millis());
    return JSON_PARSE_ERROR;
  }

  if (!release["assets"].is<JsonArray>()) {
    Serial.printf("[%lu] [OTA] No assets found\n", millis());
    return JSON_PARSE_ERROR;
  }

  latestVersion = release["tag_name"].as<std::string>();

  const auto assets = release["assets"].as<JsonArray>();
  for (const auto& asset : assets) {
    if (asset["name"] == "firmware.bin") {
      otaUrl = asset["browser_download_url"].as<std::string>();
      otaSize = asset["size"].as<size_t>();
      totalSize = otaSize;
      updateAvailable = true;
      break;
    }
  }

  if (!updateAvailable) {
    Serial.printf("[%lu] [OTA] No firmware.bin asset found\n", millis());
    return NO_UPDATE;
  }

  Serial.printf("[%lu] [OTA] Found update: %s\n", millis(), latestVersion.c_str());
  return OK;
}

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
  return latestPatch > currentPatch;
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
    Serial.printf("[%lu] [OTA] HTTP OTA Begin Failed: %s\n", millis(), esp_err_to_name(esp_err));
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
    Serial.printf("[%lu] [OTA] esp_https_ota_perform Failed: %s\n", millis(), esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return HTTP_ERROR;
  }

  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    Serial.printf("[%lu] [OTA] esp_https_ota_is_complete_data_received Failed: %s\n", millis(),
                  esp_err_to_name(esp_err));
    esp_https_ota_finish(ota_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_https_ota_finish(ota_handle);
  if (esp_err != ESP_OK) {
    Serial.printf("[%lu] [OTA] esp_https_ota_finish Failed: %s\n", millis(), esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  Serial.printf("[%lu] [OTA] Update completed\n", millis());
  return OK;
}
