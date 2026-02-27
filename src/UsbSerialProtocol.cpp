#include "UsbSerialProtocol.h"

#if ENABLE_USB_MASS_STORAGE

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>  // for logSerial (the real HWCDC)
#include <ObfuscationUtils.h>
#include <mbedtls/base64.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "JsonSettingsIO.h"
#include "RecentBooksStore.h"
#include "SpiBusMutex.h"
#include "WifiCredentialStore.h"
#include "core/features/FeatureModules.h"
#include "util/PathUtils.h"

namespace {

// Sized to fit the largest incoming command: upload_chunk with a 512-char base64 payload
// {"cmd":"upload_chunk","arg":{"data":"<512 chars>"}}  ≈ 555 bytes + null
static char s_lineBuf[768];
static int s_lineLen = 0;

// Upload state machine ────────────────────────────────────────────────────
static FsFile s_uploadFile;
static bool s_uploadInProgress = false;

// Static buffers for base64 streaming (avoids heap allocation on ESP32-C3)
// 576 raw bytes encodes to exactly 768 base64 chars; use for both download and cover.
static uint8_t s_rawBuf[576];
static uint8_t s_encBuf[780];     // 768 + padding
static uint8_t s_decodeBuf[400];  // upload chunks ≤ 512 base64 chars → ≤ 384 decoded bytes

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

// ── Base64 streaming helper ────────────────────────────────────────────────
// Opens a file and streams it base64-encoded directly to logSerial.
// Caller must have written the JSON prefix (e.g. {"ok":true,"data":"} before calling,
// and must write the closing +"}\n" after.

static bool streamFileBase64(FsFile& file) {
  while (true) {
    size_t bytesRead = 0;
    {
      SpiBusMutex::Guard guard;
      bytesRead = file.read(s_rawBuf, sizeof(s_rawBuf));
    }
    if (bytesRead == 0) break;

    size_t encLen = 0;
    mbedtls_base64_encode(s_encBuf, sizeof(s_encBuf), &encLen, s_rawBuf, bytesRead);
    logSerial.write(s_encBuf, encLen);
  }
  return true;
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

// Android expects: {"ok":true,"files":[{"name":"...","path":"...","dir":false,"size":...,"modified":0}]}
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

  bool isDir = false;
  if (root) {
    SpiBusMutex::Guard guard;
    isDir = root.isDirectory();
  }

  if (!root || !isDir) {
    if (root) {
      SpiBusMutex::Guard guard;
      root.close();
    }
    sendError("not a directory");
    return;
  }

  logSerial.print(F("{\"ok\":true,\"files\":["));
  bool first = true;

  // Normalize path for constructing full entry paths
  String base(path);
  if (!base.endsWith("/")) base += '/';

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

    String entryPath = base + name;
    JsonDocument entry;
    entry["name"] = name;
    entry["path"] = entryPath.c_str();
    entry["dir"] = entryIsDir;
    entry["size"] = entrySize;
    entry["modified"] = (uint32_t)0;
    serializeJson(entry, logSerial);
  }

  {
    SpiBusMutex::Guard guard;
    root.close();
  }
  logSerial.print(F("]}\n"));
}

// Android expects: {"ok":true,"data":"<base64-encoded file>"}
static void handleDownload(const char* path) {
  if (!PathUtils::isValidSdPath(String(path))) {
    sendError("invalid path");
    return;
  }

  FsFile file;
  bool opened = false;
  bool isDir = false;
  {
    SpiBusMutex::Guard guard;
    opened = Storage.openFileForRead("USB", path, file);
    if (opened) isDir = file.isDirectory();
  }

  if (!opened || isDir) {
    if (opened) {
      SpiBusMutex::Guard guard;
      file.close();
    }
    sendError("cannot open file");
    return;
  }

  logSerial.print(F("{\"ok\":true,\"data\":\""));
  streamFileBase64(file);
  {
    SpiBusMutex::Guard guard;
    file.close();
  }
  logSerial.print(F("\"}\n"));
}

// Android sends: {"cmd":"upload_start","arg":{"name":"file.epub","path":"/dir","size":1234}}
static void handleUploadStart(const char* name, const char* dir, uint32_t /*size*/) {
  if (s_uploadInProgress) {
    SpiBusMutex::Guard guard;
    s_uploadFile.close();
    s_uploadInProgress = false;
  }

  String destPath(dir);
  if (!destPath.endsWith("/")) destPath += '/';
  destPath += name;

  if (!PathUtils::isValidSdPath(destPath)) {
    sendError("invalid path");
    return;
  }

  bool opened = false;
  {
    SpiBusMutex::Guard guard;
    opened = Storage.openFileForWrite("USB", destPath.c_str(), s_uploadFile);
  }

  if (!opened) {
    sendError("cannot open file for write");
    return;
  }

  s_uploadInProgress = true;
  sendOk();
}

// Android sends 512-char base64 chunks → decodes to ≤384 bytes
static void handleUploadChunk(const char* b64data) {
  if (!s_uploadInProgress) {
    sendError("no upload in progress");
    return;
  }

  const size_t b64len = strlen(b64data);
  size_t decodedLen = 0;
  const int rc = mbedtls_base64_decode(s_decodeBuf, sizeof(s_decodeBuf), &decodedLen, (const uint8_t*)b64data, b64len);
  if (rc != 0) {
    SpiBusMutex::Guard guard;
    s_uploadFile.close();
    s_uploadInProgress = false;
    sendError("base64 decode error");
    return;
  }

  bool writeOk = false;
  {
    SpiBusMutex::Guard guard;
    writeOk = (s_uploadFile.write(s_decodeBuf, decodedLen) == decodedLen);
  }

  if (!writeOk) {
    SpiBusMutex::Guard guard;
    s_uploadFile.close();
    s_uploadInProgress = false;
    sendError("write failed");
    return;
  }

  sendOk();
}

static void handleUploadDone() {
  if (!s_uploadInProgress) {
    sendError("no upload in progress");
    return;
  }
  {
    SpiBusMutex::Guard guard;
    s_uploadFile.close();
  }
  s_uploadInProgress = false;
  sendOk();
}

// Android sends: {"cmd":"delete","arg":["/path/a","/path/b"]}
static void handleDelete(JsonArrayConst paths) {
  bool anyFailed = false;
  for (JsonVariantConst entry : paths) {
    const char* path = entry.as<const char*>();
    if (!path || !PathUtils::isValidSdPath(String(path))) {
      anyFailed = true;
      continue;
    }
    bool ok = false;
    {
      SpiBusMutex::Guard guard;
      ok = Storage.remove(path);
      if (!ok) ok = Storage.removeDir(path);
    }
    if (!ok) anyFailed = true;
  }
  if (anyFailed) {
    sendError("one or more deletes failed");
  } else {
    sendOk();
  }
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

// Android sends: {"cmd":"rename","arg":{"from":"/old/path","to":"/new/path"}}
static void handleRename(const char* from, const char* to) {
  if (!PathUtils::isValidSdPath(String(from)) || !PathUtils::isValidSdPath(String(to))) {
    sendError("invalid path");
    return;
  }
  bool ok = false;
  {
    SpiBusMutex::Guard guard;
    ok = Storage.rename(from, to);
  }
  if (!ok) {
    sendError("rename failed");
    return;
  }
  sendOk();
}

// Android sends: {"cmd":"move","arg":{"from":"/src/file.epub","to":"/dst/dir"}}
// "to" is a destination directory; preserve the source filename.
static void handleMove(const char* from, const char* toDir) {
  if (!PathUtils::isValidSdPath(String(from)) || !PathUtils::isValidSdPath(String(toDir))) {
    sendError("invalid path");
    return;
  }
  String fromStr(from);
  const int lastSlash = fromStr.lastIndexOf('/');
  const String filename = (lastSlash >= 0) ? fromStr.substring(lastSlash + 1) : fromStr;

  String dest(toDir);
  if (!dest.endsWith("/")) dest += '/';
  dest += filename;

  bool ok = false;
  {
    SpiBusMutex::Guard guard;
    ok = Storage.rename(from, dest.c_str());
  }
  if (!ok) {
    sendError("move failed");
    return;
  }
  sendOk();
}

static void handleSettingsGet() {
  JsonDocument doc;
  buildSettingsDoc(doc);
  logSerial.print(F("{\"ok\":true,\"settings\":"));
  serializeJson(doc, logSerial);
  logSerial.print(F("}\n"));
}

static void handleSettingsSet(JsonObjectConst incoming) {
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

// Android expects:
// {"ok":true,"books":[{"path":"...","title":"...","author":"...","last_position":"...","last_opened":0}]}
static void handleRecent() {
  logSerial.print(F("{\"ok\":true,\"books\":["));
  bool first = true;
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (!first) logSerial.write(',');
    first = false;
    JsonDocument entry;
    entry["path"] = book.path.c_str();
    entry["title"] = book.title.c_str();
    entry["author"] = book.author.c_str();
    entry["last_position"] = "";         // not yet persisted
    entry["last_opened"] = (uint32_t)0;  // not yet persisted
    serializeJson(entry, logSerial);
  }
  logSerial.print(F("]}\n"));
}

// Android expects: {"ok":true,"data":"<base64-encoded BMP>"} or {"ok":false,"error":"..."}
static void handleCover(const char* path) {
  if (!PathUtils::isValidSdPath(String(path))) {
    sendError("invalid path");
    return;
  }

  // Find cached cover BMP path (check recent books first, then feature modules)
  std::string coverPath;
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (book.path == path && !book.coverBmpPath.empty()) {
      coverPath = book.coverBmpPath;
      break;
    }
  }
  if (coverPath.empty()) {
    core::FeatureModules::tryGetDocumentCoverPath(String(path), coverPath);
  }

  if (coverPath.empty()) {
    sendError("no cover available");
    return;
  }

  FsFile file;
  bool opened = false;
  {
    SpiBusMutex::Guard guard;
    opened = Storage.openFileForRead("USB", coverPath.c_str(), file);
  }

  if (!opened) {
    sendError("cover file not found");
    return;
  }

  logSerial.print(F("{\"ok\":true,\"data\":\""));
  streamFileBase64(file);
  {
    SpiBusMutex::Guard guard;
    file.close();
  }
  logSerial.print(F("\"}\n"));
}

// Saves credentials so they're picked up on next WiFi connection attempt.
static void handleWifiConnect(const char* ssid, const char* password) {
  if (!ssid || ssid[0] == '\0') {
    sendError("SSID required");
    return;
  }
  WIFI_STORE.addCredential(ssid, password ? password : "");
  WIFI_STORE.saveToFile();
  sendOk();
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
    handleList(cmd["arg"] | "/");
  } else if (strcmp(name, "download") == 0) {
    handleDownload(cmd["arg"] | "");
  } else if (strcmp(name, "upload_start") == 0) {
    const JsonObjectConst arg = cmd["arg"].as<JsonObjectConst>();
    handleUploadStart(arg["name"] | "", arg["path"] | "/", arg["size"] | (uint32_t)0);
  } else if (strcmp(name, "upload_chunk") == 0) {
    handleUploadChunk(cmd["arg"]["data"] | "");
  } else if (strcmp(name, "upload_done") == 0) {
    handleUploadDone();
  } else if (strcmp(name, "delete") == 0) {
    handleDelete(cmd["arg"].as<JsonArrayConst>());
  } else if (strcmp(name, "mkdir") == 0) {
    handleMkdir(cmd["arg"] | "");
  } else if (strcmp(name, "rename") == 0) {
    const JsonObjectConst arg = cmd["arg"].as<JsonObjectConst>();
    handleRename(arg["from"] | "", arg["to"] | "");
  } else if (strcmp(name, "move") == 0) {
    const JsonObjectConst arg = cmd["arg"].as<JsonObjectConst>();
    handleMove(arg["from"] | "", arg["to"] | "");
  } else if (strcmp(name, "settings_get") == 0) {
    handleSettingsGet();
  } else if (strcmp(name, "settings_set") == 0) {
    handleSettingsSet(cmd["arg"].as<JsonObjectConst>());
  } else if (strcmp(name, "recent") == 0) {
    handleRecent();
  } else if (strcmp(name, "cover") == 0) {
    handleCover(cmd["arg"] | "");
  } else if (strcmp(name, "wifi_connect") == 0) {
    const JsonObjectConst arg = cmd["arg"].as<JsonObjectConst>();
    handleWifiConnect(arg["ssid"] | "", arg["password"] | "");
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

void UsbSerialProtocol::reset() {
  s_lineLen = 0;
  if (s_uploadInProgress) {
    SpiBusMutex::Guard guard;
    s_uploadFile.close();
    s_uploadInProgress = false;
  }
}

#endif  // ENABLE_USB_MASS_STORAGE
