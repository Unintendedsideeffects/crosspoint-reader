#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class OtaUpdater {
 public:
  struct FeatureStoreEntry {
    std::string id;
    std::string displayName;
    std::string version;
    std::string featureFlags;
    std::string downloadUrl;
    std::string checksum;
    size_t binarySize = 0;
    bool compatible = true;
    std::string compatibilityError;
  };

 private:
  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;
  bool render = false;
  bool factoryResetOnInstall = false;
  std::vector<FeatureStoreEntry> featureStoreEntries;
  std::string selectedBundleId;
  std::string selectedFeatureFlags;
  std::string selectedChecksum;
  std::string lastError;

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
    CATALOG_UNAVAILABLE_ERROR,
    BUNDLE_UNAVAILABLE_ERROR,
    INCOMPATIBLE_BUNDLE_ERROR,
  };

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  bool getRender() const { return render; }

  bool willFactoryResetOnInstall() const { return factoryResetOnInstall; }
  const std::vector<FeatureStoreEntry>& getFeatureStoreEntries() const { return featureStoreEntries; }
  const std::string& getSelectedBundleId() const { return selectedBundleId; }
  const std::string& getSelectedFeatureFlags() const { return selectedFeatureFlags; }
  const std::string& getSelectedChecksum() const { return selectedChecksum; }
  const std::string& getLastError() const { return lastError; }
  bool hasFeatureStoreCatalog() const { return !featureStoreEntries.empty(); }
  void selectFeatureStoreBundleByIndex(size_t index);
  bool loadFeatureStoreCatalog();

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  OtaUpdaterError checkForUpdate();
  OtaUpdaterError installUpdate();
};
