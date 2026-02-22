#include "FactoryResetUtils.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "SpiBusMutex.h"

namespace FactoryResetUtils {
namespace {
constexpr char kCrossPointDataDir[] = "/.crosspoint";
constexpr size_t kPathBufferSize = 192;
constexpr const char* kMetadataFilesToRemove[] = {
    "/.crosspoint/settings.bin", "/.crosspoint/settings.json",  "/.crosspoint/settings.bin.bak",
    "/.crosspoint/state.bin",    "/.crosspoint/state.json",     "/.crosspoint/state.bin.bak",
    "/.crosspoint/recent.bin",   "/.crosspoint/recent.json",    "/.crosspoint/recent.bin.bak",
    "/.crosspoint/wifi.bin",     "/.crosspoint/wifi.json",      "/.crosspoint/wifi.bin.bak",
    "/.crosspoint/koreader.bin", "/.crosspoint/koreader.json",  "/.crosspoint/koreader.bin.bak",
    "/.crosspoint/language.bin", "/.crosspoint/usb-msc-active",
};
constexpr const char* kCacheDirectoryPrefixes[] = {"epub_", "xtc_", "txt_", "md_"};

bool hasCachePrefix(const char* name) {
  for (const char* prefix : kCacheDirectoryPrefixes) {
    const size_t len = strlen(prefix);
    if (strncmp(name, prefix, len) == 0) {
      return true;
    }
  }
  return false;
}
}  // namespace

bool resetCrossPointMetadataPreservingContent() {
  int removedCount = 0;
  int failedCount = 0;

  SpiBusMutex::Guard guard;

  if (!Storage.exists(kCrossPointDataDir) && !Storage.mkdir(kCrossPointDataDir)) {
    LOG_ERR("RESET", "Failed to create %s", kCrossPointDataDir);
    return false;
  }

  for (const char* path : kMetadataFilesToRemove) {
    if (!Storage.exists(path)) {
      continue;
    }
    if (Storage.remove(path)) {
      removedCount++;
    } else {
      LOG_ERR("RESET", "Failed to remove metadata file: %s", path);
      failedCount++;
    }
  }

  FsFile root = Storage.open(kCrossPointDataDir);
  if (!root || !root.isDirectory()) {
    LOG_ERR("RESET", "Failed to open cache directory: %s", kCrossPointDataDir);
    if (root) {
      root.close();
    }
    return false;
  }

  char name[128];
  for (FsFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    entry.getName(name, sizeof(name));
    const bool isDirectory = entry.isDirectory();
    entry.close();

    if (!isDirectory || !hasCachePrefix(name)) {
      continue;
    }

    char fullPath[kPathBufferSize];
    const int written = snprintf(fullPath, sizeof(fullPath), "%s/%s", kCrossPointDataDir, name);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(fullPath)) {
      LOG_ERR("RESET", "Skipping cache dir with invalid path: %s", name);
      failedCount++;
      continue;
    }

    if (Storage.removeDir(fullPath)) {
      removedCount++;
    } else {
      LOG_ERR("RESET", "Failed to remove cache directory: %s", fullPath);
      failedCount++;
    }
  }
  root.close();

  if (failedCount > 0) {
    LOG_ERR("RESET", "Factory reset cleanup incomplete (removed=%d failed=%d)", removedCount, failedCount);
    return false;
  }

  LOG_INF("RESET", "Factory reset cleanup complete (removed=%d metadata/cache items)", removedCount);
  return true;
}
}  // namespace FactoryResetUtils
