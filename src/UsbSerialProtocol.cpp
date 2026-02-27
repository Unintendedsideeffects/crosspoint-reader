#include "UsbSerialProtocol.h"

#if ENABLE_USB_MASS_STORAGE

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>  // for logSerial (the real HWCDC)
#include <ObfuscationUtils.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "JsonSettingsIO.h"
#include "RecentBooksStore.h"
#include "SpiBusMutex.h"
#include "util/PathUtils.h"

namespace {

static char s_lineBuf[512];
static int s_lineLen = 0;

// ── Response helpers ───────────────────────────────────────────────────────

static void sendOk() { logSerial.print(F("{\"ok\":true}\n")); }

static void sendError(const char* msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg;
  serializeJson(doc, logSerial);
  logSerial.write('\n');
}

// ── Settings serialization ─────────────────────────────────────────────────
// Mirrors JsonSettingsIO::saveSettings but populates a JsonDocument directly.

static void buildSettingsDoc(JsonDocument& doc) {
  const CrossPointSettings& s = SETTINGS;
  doc["sleepScreen"] = s.sleepScreen;
  doc["sleepScreenSource"] = s.sleepScreenSource;
  doc["sleepScreenCoverMode"] = s.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = s.sleepScreenCoverFilter;
  doc["statusBar"] = s.statusBar;
  doc["statusBarChapterPageCount"] = s.statusBarChapterPageCount;
  doc["statusBarBookProgressPercentage"] = s.statusBarBookProgressPercentage;
  doc["statusBarProgressBar"] = s.statusBarProgressBar;
  doc["statusBarProgressBarThickness"] = s.statusBarProgressBarThickness;
  doc["statusBarTitle"] = s.statusBarTitle;
  doc["statusBarBattery"] = s.statusBarBattery;
  doc["extraParagraphSpacing"] = s.extraParagraphSpacing;
  doc["textAntiAliasing"] = s.textAntiAliasing;
  doc["shortPwrBtn"] = s.shortPwrBtn;
  doc["orientation"] = s.orientation;
  doc["frontButtonLayout"] = s.frontButtonLayout;
  doc["sideButtonLayout"] = s.sideButtonLayout;
  doc["frontButtonBack"] = s.frontButtonBack;
  doc["frontButtonConfirm"] = s.frontButtonConfirm;
  doc["frontButtonLeft"] = s.frontButtonLeft;
  doc["frontButtonRight"] = s.frontButtonRight;
  doc["fontFamily"] = s.fontFamily;
  doc["fontSize"] = s.fontSize;
  doc["lineSpacing"] = s.lineSpacing;
  doc["paragraphAlignment"] = s.paragraphAlignment;
  doc["sleepTimeout"] = s.sleepTimeout;
  doc["refreshFrequency"] = s.refreshFrequency;
  doc["screenMargin"] = s.screenMargin;
  doc["opdsServerUrl"] = s.opdsServerUrl;
  doc["opdsUsername"] = s.opdsUsername;
  doc["opdsPassword_obf"] = obfuscation::obfuscateToBase64(s.opdsPassword);
  doc["hideBatteryPercentage"] = s.hideBatteryPercentage;
  doc["longPressChapterSkip"] = s.longPressChapterSkip;
  doc["hyphenationEnabled"] = s.hyphenationEnabled;
  doc["backgroundServerOnCharge"] = s.backgroundServerOnCharge;
  doc["todoFallbackCover"] = s.todoFallbackCover;
  doc["timeMode"] = s.timeMode;
  doc["timeZoneOffset"] = s.timeZoneOffset;
  doc["lastTimeSyncEpoch"] = s.lastTimeSyncEpoch;
  doc["releaseChannel"] = s.releaseChannel;
  doc["uiTheme"] = s.uiTheme;
  doc["fadingFix"] = s.fadingFix;
  doc["darkMode"] = s.darkMode;
  doc["embeddedStyle"] = s.embeddedStyle;
  doc["usbMscPromptOnConnect"] = s.usbMscPromptOnConnect;
  doc["userFontPath"] = s.userFontPath;
  doc["selectedOtaBundle"] = s.selectedOtaBundle;
  doc["installedOtaBundle"] = s.installedOtaBundle;
  doc["installedOtaFeatureFlags"] = s.installedOtaFeatureFlags;
  doc["deviceName"] = s.deviceName;
}

// ── Command handlers ───────────────────────────────────────────────────────

static void handleStatus() {
  JsonDocument resp;
  resp["ok"] = true;
  resp["version"] = CROSSPOINT_VERSION;
  resp["freeHeap"] = (uint32_t)ESP.getFreeHeap();
  resp["uptime"] = millis() / 1000;
  resp["openBook"] = APP_STATE.openEpubPath.c_str();
  serializeJson(resp, logSerial);
  logSerial.write('\n');
}

static void handleList(const char* path) {
  if (!PathUtils::isValidSdPath(String(path))) {
    sendError("invalid path");
    return;
  }

  FsFile root;
  {
    SpiBusMutex::Guard guard;
    root = Storage.open(path);
  }

  if (!root) {
    sendError("cannot open directory");
    return;
  }

  bool isDir = false;
  {
    SpiBusMutex::Guard guard;
    isDir = root.isDirectory();
  }
  if (!isDir) {
    {
      SpiBusMutex::Guard guard;
      root.close();
    }
    sendError("not a directory");
    return;
  }

  logSerial.print(F("{\"ok\":true,\"files\":["));
  bool first = true;

  while (true) {
    char name[256] = {0};
    bool entryIsDir = false;
    uint32_t entrySize = 0;
    bool valid = false;

    {
      SpiBusMutex::Guard guard;
      FsFile file = root.openNextFile();
      if (!file) break;
      file.getName(name, sizeof(name));
      entryIsDir = file.isDirectory();
      entrySize = entryIsDir ? 0 : (uint32_t)file.size();
      valid = true;
      file.close();
    }

    if (!valid) break;
    if (name[0] == '.') continue;  // skip hidden entries

    if (!first) logSerial.write(',');
    first = false;

    JsonDocument entry;
    entry["name"] = name;
    entry["size"] = entrySize;
    entry["isDirectory"] = entryIsDir;
    serializeJson(entry, logSerial);
  }

  {
    SpiBusMutex::Guard guard;
    root.close();
  }
  logSerial.print(F("]}\n"));
}

static void handleRead(const char* path, uint32_t offset, uint32_t length) {
  if (!PathUtils::isValidSdPath(String(path))) {
    sendError("invalid path");
    return;
  }

  FsFile file;
  bool opened = false;
  uint32_t fileSize = 0;
  bool isDir = false;
  {
    SpiBusMutex::Guard guard;
    opened = Storage.openFileForRead("USB", path, file);
    if (opened) {
      isDir = file.isDirectory();
      fileSize = isDir ? 0 : (uint32_t)file.size();
    }
  }

  if (!opened || isDir) {
    if (opened) {
      SpiBusMutex::Guard guard;
      file.close();
    }
    sendError("cannot open file");
    return;
  }

  if (offset > fileSize) offset = fileSize;
  const uint32_t available = fileSize - offset;
  if (length == 0 || length > available) length = available;

  {
    SpiBusMutex::Guard guard;
    file.seek(offset);
  }

  // Send header before raw bytes
  logSerial.printf("{\"ok\":true,\"bytes\":%lu}\n", (unsigned long)length);

  uint8_t buf[512];
  uint32_t remaining = length;
  while (remaining > 0) {
    const uint32_t toRead = remaining < sizeof(buf) ? remaining : sizeof(buf);
    size_t bytesRead = 0;
    {
      SpiBusMutex::Guard guard;
      bytesRead = file.read(buf, toRead);
    }
    if (bytesRead == 0) break;
    logSerial.write(buf, bytesRead);
    remaining -= (uint32_t)bytesRead;
  }

  {
    SpiBusMutex::Guard guard;
    file.close();
  }
}

static void handleWrite(const char* path, uint32_t size) {
  if (!PathUtils::isValidSdPath(String(path))) {
    sendError("invalid path");
    return;
  }

  FsFile file;
  bool opened = false;
  {
    SpiBusMutex::Guard guard;
    opened = Storage.openFileForWrite("USB", path, file);
  }

  if (!opened) {
    sendError("cannot open file for write");
    return;
  }

  logSerial.print(F("{\"ok\":true,\"ready\":true}\n"));
  logSerial.flush();

  logSerial.setTimeout(5000);

  uint8_t buf[512];
  uint32_t remaining = size;
  while (remaining > 0) {
    const uint32_t toRead = remaining < sizeof(buf) ? remaining : sizeof(buf);
    const size_t bytesRead = logSerial.readBytes(buf, toRead);
    if (bytesRead == 0) {
      {
        SpiBusMutex::Guard guard;
        file.close();
      }
      logSerial.setTimeout(1000);
      sendError("timeout reading data");
      return;
    }
    bool writeOk = false;
    {
      SpiBusMutex::Guard guard;
      writeOk = (file.write(buf, bytesRead) == bytesRead);
    }
    if (!writeOk) {
      {
        SpiBusMutex::Guard guard;
        file.close();
      }
      logSerial.setTimeout(1000);
      sendError("write failed");
      return;
    }
    remaining -= (uint32_t)bytesRead;
  }

  {
    SpiBusMutex::Guard guard;
    file.close();
  }
  logSerial.setTimeout(1000);
  sendOk();
}

static void handleDelete(const char* path) {
  if (!PathUtils::isValidSdPath(String(path))) {
    sendError("invalid path");
    return;
  }
  bool ok = false;
  {
    SpiBusMutex::Guard guard;
    ok = Storage.remove(path);
  }
  if (!ok) {
    sendError("delete failed");
    return;
  }
  sendOk();
}

static void handleMkdir(const char* path) {
  if (!PathUtils::isValidSdPath(String(path))) {
    sendError("invalid path");
    return;
  }
  bool ok = false;
  {
    SpiBusMutex::Guard guard;
    ok = Storage.mkdir(path);
  }
  if (!ok) {
    sendError("mkdir failed");
    return;
  }
  sendOk();
}

static void handleGetSettings() {
  JsonDocument doc;
  buildSettingsDoc(doc);
  logSerial.print(F("{\"ok\":true,\"settings\":"));
  serializeJson(doc, logSerial);
  logSerial.print(F("}\n"));
}

static void handleSetSettings(JsonObjectConst incoming) {
  // Build the current settings as JSON, overlay the incoming changes,
  // then round-trip through loadSettings to apply clamping and validation.
  JsonDocument merged;
  buildSettingsDoc(merged);
  for (auto kv : incoming) {
    merged[kv.key()] = kv.value();
  }
  String mergedJson;
  serializeJson(merged, mergedJson);

  JsonSettingsIO::loadSettings(SETTINGS, mergedJson.c_str());

  bool saved = false;
  {
    SpiBusMutex::Guard guard;
    saved = SETTINGS.saveToFile();
  }
  if (!saved) {
    sendError("settings save failed");
    return;
  }
  sendOk();
}

static void handleGetRecent() {
  logSerial.print(F("{\"ok\":true,\"recent\":["));
  bool first = true;
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (!first) logSerial.write(',');
    first = false;
    JsonDocument entry;
    entry["path"] = book.path.c_str();
    entry["title"] = book.title.c_str();
    entry["author"] = book.author.c_str();
    serializeJson(entry, logSerial);
  }
  logSerial.print(F("]}\n"));
}

// ── Command dispatcher ─────────────────────────────────────────────────────

static void processCommand(const char* line) {
  JsonDocument cmd;
  const auto err = deserializeJson(cmd, line);
  if (err) {
    sendError("parse error");
    return;
  }

  const char* name = cmd["cmd"] | "";

  if (strcmp(name, "status") == 0) {
    handleStatus();
  } else if (strcmp(name, "list") == 0) {
    handleList(cmd["path"] | "/");
  } else if (strcmp(name, "read") == 0) {
    handleRead(cmd["path"] | "", cmd["offset"] | (uint32_t)0, cmd["length"] | (uint32_t)0);
  } else if (strcmp(name, "write") == 0) {
    handleWrite(cmd["path"] | "", cmd["size"] | (uint32_t)0);
  } else if (strcmp(name, "delete") == 0) {
    handleDelete(cmd["path"] | "");
  } else if (strcmp(name, "mkdir") == 0) {
    handleMkdir(cmd["path"] | "");
  } else if (strcmp(name, "get_settings") == 0) {
    handleGetSettings();
  } else if (strcmp(name, "set_settings") == 0) {
    handleSetSettings(cmd["settings"].as<JsonObjectConst>());
  } else if (strcmp(name, "get_recent") == 0) {
    handleGetRecent();
  } else {
    sendError("unknown command");
  }
}

}  // namespace

// ── Public interface ───────────────────────────────────────────────────────

void UsbSerialProtocol::loop() {
  while (logSerial.available()) {
    const int c = logSerial.read();
    if (c < 0) break;
    if (c == '\r') continue;  // tolerate CRLF line endings
    if (c == '\n') {
      s_lineBuf[s_lineLen] = '\0';
      if (s_lineLen > 0) processCommand(s_lineBuf);
      s_lineLen = 0;
      return;  // process one command per loop() call
    }
    if (s_lineLen < static_cast<int>(sizeof(s_lineBuf)) - 1) {
      s_lineBuf[s_lineLen++] = static_cast<char>(c);
    } else {
      s_lineLen = 0;  // line too long — reset and wait for next newline
    }
  }
}

void UsbSerialProtocol::reset() { s_lineLen = 0; }

#endif  // ENABLE_USB_MASS_STORAGE
