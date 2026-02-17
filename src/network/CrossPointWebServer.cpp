#include "CrossPointWebServer.h"

#include <ArduinoJson.h>
#include <FeatureFlags.h>
#include <FsHelpers.h>
#if ENABLE_EPUB_SUPPORT
#include <Epub.h>
#endif
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "FeatureManifest.h"
#include "RecentBooksStore.h"
#include "SettingsList.h"
#include "SpiBusMutex.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "html/FilesPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#if ENABLE_WEB_POKEDEX_PLUGIN
#include "html/PokedexPluginPageHtml.generated.h"
#endif
#include "html/SettingsPageHtml.generated.h"
#include "util/PathUtils.h"
#include "util/StringUtils.h"

namespace {
// Folders/files to hide from the web interface file browser
// Note: Items starting with "." are automatically hidden
const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};
constexpr size_t HIDDEN_ITEMS_COUNT = sizeof(HIDDEN_ITEMS) / sizeof(HIDDEN_ITEMS[0]);
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;

// Static pointer for WebSocket callback (WebSocketsServer requires C-style callback)
CrossPointWebServer* wsInstance = nullptr;

// WebSocket upload state
FsFile wsUploadFile;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
size_t wsLastProgressSent = 0;
unsigned long wsUploadStartTime = 0;
bool wsUploadInProgress = false;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;

// Helper function to clear epub cache after upload
void clearEpubCacheIfNeeded(const String& filePath) {
#if ENABLE_EPUB_SUPPORT
  // Only clear cache for .epub files
  if (StringUtils::checkFileExtension(filePath, ".epub")) {
    Epub(filePath.c_str(), "/.crosspoint").clearCache();
    LOG_DBG("WEB", "Cleared epub cache for: %s", filePath.c_str());
  }
#else
  (void)filePath;
#endif
}

// Helper to invalidate sleep image cache when /sleep/ or sleep images are modified
void invalidateSleepCacheIfNeeded(const String& filePath) {
  String lowerPath = filePath;
  lowerPath.toLowerCase();
  if (lowerPath.equals("/sleep.bmp") || lowerPath.equals("/sleep.png") || lowerPath.equals("/sleep.jpg") ||
      lowerPath.equals("/sleep.jpeg") || lowerPath.startsWith("/sleep/") || lowerPath.equals("/sleep")) {
    invalidateSleepImageCache();
  }
}
}  // namespace

// File listing page template - now using generated headers:
// - HomePageHtml (from html/HomePage.html)
// - FilesPageHeaderHtml (from html/FilesPageHeader.html)
// - FilesPageFooterHtml (from html/FilesPageFooter.html)
CrossPointWebServer::CrossPointWebServer() {}

CrossPointWebServer::~CrossPointWebServer() { stop(); }

void CrossPointWebServer::begin() {
  if (running) {
    LOG_DBG("WEB", "Web server already running");
    return;
  }

  // Check if we have a valid network connection (either STA connected or AP mode)
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);  // AP is running

  if (!isStaConnected && !isInApMode) {
    LOG_DBG("WEB", "Cannot start webserver - no valid network (mode=%d, status=%d)", wifiMode, WiFi.status());
    return;
  }

  // Store AP mode flag for later use (e.g., in handleStatus)
  apMode = isInApMode;

  LOG_DBG("WEB", "[MEM] Free heap before begin: %d bytes", ESP.getFreeHeap());
  LOG_DBG("WEB", "Network mode: %s", apMode ? "AP" : "STA");

  LOG_DBG("WEB", "Creating web server on port %d...", port);
  server.reset(new WebServer(port));

  // Disable WiFi sleep to improve responsiveness and prevent 'unreachable' errors.
  // This is critical for reliable web server operation on ESP32.
  WiFi.setSleep(false);

  // Note: WebServer class doesn't have setNoDelay() in the standard ESP32 library.
  // We rely on disabling WiFi sleep for responsiveness.

  LOG_DBG("WEB", "[MEM] Free heap after WebServer allocation: %d bytes", ESP.getFreeHeap());

  if (!server) {
    LOG_ERR("WEB", "Failed to create WebServer!");
    return;
  }

  // Setup routes
  LOG_DBG("WEB", "Setting up routes...");
  server->on("/", HTTP_GET, [this] { handleRoot(); });
  server->on("/files", HTTP_GET, [this] { handleFileList(); });

  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/plugins", HTTP_GET, [this] { handlePlugins(); });
  // Backward-compatible alias while tooling migrates terminology.
  server->on("/api/features", HTTP_GET, [this] { handlePlugins(); });
  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
  server->on("/download", HTTP_GET, [this] { handleDownload(); });

  // Upload endpoint with special handling for multipart form data
  server->on("/upload", HTTP_POST, [this] { handleUploadPost(); }, [this] { handleUpload(); });

  // Create folder endpoint
  server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

  // Rename file endpoint
  server->on("/rename", HTTP_POST, [this] { handleRename(); });

  // Move file endpoint
  server->on("/move", HTTP_POST, [this] { handleMove(); });

  // Delete file/folder endpoint
  server->on("/delete", HTTP_POST, [this] { handleDelete(); });

  // Settings endpoints
  server->on("/settings", HTTP_GET, [this] { handleSettingsPage(); });
#if ENABLE_WEB_POKEDEX_PLUGIN
  server->on("/plugins/pokedex", HTTP_GET, [this] { handlePokedexPluginPage(); });
#endif
  server->on("/api/settings", HTTP_GET, [this] { handleGetSettings(); });
  server->on("/api/settings", HTTP_POST, [this] { handlePostSettings(); });

  // WiFi endpoints
#if ENABLE_WEB_WIFI_SETUP
  server->on("/api/wifi/scan", HTTP_GET, [this] { handleWifiScan(); });
  server->on("/api/wifi/connect", HTTP_POST, [this] { handleWifiConnect(); });
  server->on("/api/wifi/forget", HTTP_POST, [this] { handleWifiForget(); });
#endif

  // API endpoints for web UI (recent books, cover images)
  server->on("/api/recent", HTTP_GET, [this] { handleRecentBooks(); });
  server->on("/api/cover", HTTP_GET, [this] { handleCover(); });

  server->onNotFound([this] { handleNotFound(); });
  LOG_DBG("WEB", "[MEM] Free heap after route setup: %d bytes", ESP.getFreeHeap());

  server->begin();

  // Start WebSocket server for fast binary uploads
  LOG_DBG("WEB", "Starting WebSocket server on port %d...", wsPort);
  wsServer.reset(new WebSocketsServer(wsPort));
  wsInstance = const_cast<CrossPointWebServer*>(this);
  wsServer->begin();
  wsServer->onEvent(wsEventCallback);
  LOG_DBG("WEB", "WebSocket server started");

  udpActive = udp.begin(LOCAL_UDP_PORT);
  LOG_DBG("WEB", "Discovery UDP %s on port %d", udpActive ? "enabled" : "failed", LOCAL_UDP_PORT);

  running = true;

  LOG_DBG("WEB", "Web server started on port %d", port);
  // Show the correct IP based on network mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  LOG_DBG("WEB", "Access at http://%s/", ipAddr.c_str());
  LOG_DBG("WEB", "WebSocket at ws://%s:%d/", ipAddr.c_str(), wsPort);
  LOG_DBG("WEB", "[MEM] Free heap after server.begin(): %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::stop() {
  if (!running || !server) {
    LOG_DBG("WEB", "stop() called but already stopped (running=%d, server=%p)", running, server.get());
    return;
  }

  LOG_DBG("WEB", "STOP INITIATED - setting running=false first");
  running = false;  // Set this FIRST to prevent handleClient from using server

  LOG_DBG("WEB", "[MEM] Free heap before stop: %d bytes", ESP.getFreeHeap());

  // Close any in-progress WebSocket upload
  if (wsUploadInProgress && wsUploadFile) {
    wsUploadFile.close();
    wsUploadInProgress = false;
  }

  // Stop WebSocket server
  if (wsServer) {
    LOG_DBG("WEB", "Stopping WebSocket server...");
    wsServer->close();
    wsServer.reset();
    wsInstance = nullptr;
    LOG_DBG("WEB", "WebSocket server stopped");
  }

  if (udpActive) {
    udp.stop();
    udpActive = false;
  }

  // Brief delay to allow any in-flight handleClient() calls to complete
  delay(20);

  server->stop();
  LOG_DBG("WEB", "[MEM] Free heap after server->stop(): %d bytes", ESP.getFreeHeap());

  // Brief delay before deletion
  delay(10);

  server.reset();
  LOG_DBG("WEB", "Web server stopped and deleted");
  LOG_DBG("WEB", "[MEM] Free heap after delete server: %d bytes", ESP.getFreeHeap());

  // Note: Static upload variables (uploadFileName, uploadPath, uploadError) are declared
  // later in the file and will be cleared when they go out of scope or on next upload
  LOG_DBG("WEB", "[MEM] Free heap final: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::handleClient() {
  static unsigned long lastDebugPrint = 0;

  // Check running flag FIRST before accessing server
  if (!running) {
    return;
  }

  // Double-check server pointer is valid
  if (!server) {
    LOG_DBG("WEB", "WARNING: handleClient called with null server!");
    return;
  }

  // Print debug every 10 seconds to confirm handleClient is being called
  if (millis() - lastDebugPrint > 10000) {
    LOG_DBG("WEB", "handleClient active, server running on port %d", port);
    lastDebugPrint = millis();
  }

  server->handleClient();

  // Handle WebSocket events
  if (wsServer) {
    wsServer->loop();
  }

  // Respond to discovery broadcasts
  if (udpActive) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[16];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "hello") == 0) {
          String hostname = WiFi.getHostname();
          if (hostname.isEmpty()) {
            hostname = "crosspoint";
          }
          String message = "crosspoint (on " + hostname + ");" + String(wsPort);
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
          udp.endPacket();
        }
      }
    }
  }
}

CrossPointWebServer::WsUploadStatus CrossPointWebServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

static_assert(HomePageHtmlCompressedSize == sizeof(HomePageHtml), "Home page compressed size mismatch");
static_assert(FilesPageHtmlCompressedSize == sizeof(FilesPageHtml), "Files page compressed size mismatch");
static_assert(SettingsPageHtmlCompressedSize == sizeof(SettingsPageHtml), "Settings page compressed size mismatch");
#if ENABLE_WEB_POKEDEX_PLUGIN
static_assert(PokedexPluginPageHtmlCompressedSize == sizeof(PokedexPluginPageHtml),
              "Pokedex page compressed size mismatch");
#endif

static bool isGzipPayload(const char* data, size_t len) {
  return len >= 2 && static_cast<unsigned char>(data[0]) == 0x1f && static_cast<unsigned char>(data[1]) == 0x8b;
}

static void sendPrecompressedHtml(WebServer* server, const char* data, size_t compressedLen) {
  if (!isGzipPayload(data, compressedLen)) {
    LOG_ERR("WEB", "Attempted to serve non-gzip HTML payload");
    server->send(500, "text/plain", "Invalid precompressed HTML payload");
    return;
  }
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "text/html; charset=utf-8", data, compressedLen);
}

void CrossPointWebServer::handleRoot() const {
  sendPrecompressedHtml(server.get(), HomePageHtml, HomePageHtmlCompressedSize);
  LOG_DBG("WEB", "Served root page");
}

void CrossPointWebServer::handleNotFound() const {
  String message = "404 Not Found\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void CrossPointWebServer::handleStatus() const {
  // Get correct IP based on AP vs STA mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  const bool staConnected = WiFi.status() == WL_CONNECTED;
  const String wifiStatus = apMode ? "AP Mode" : (staConnected ? "Connected" : "Disconnected");

  JsonDocument doc;
  doc["version"] = CROSSPOINT_VERSION;
  doc["wifiStatus"] = wifiStatus;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handlePlugins() const { server->send(200, "application/json", FeatureManifest::toJson()); }

void CrossPointWebServer::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  FsFile root;
  {
    SpiBusMutex::Guard guard;
    root = Storage.open(path);
  }

  if (!root) {
    LOG_DBG("WEB", "Failed to open directory: %s", path);
    return;
  }

  if (!root.isDirectory()) {
    LOG_DBG("WEB", "Not a directory: %s", path);
    root.close();
    return;
  }

  LOG_DBG("WEB", "Scanning files in: %s", path);

  while (true) {
    FileInfo info;
    bool shouldHide = false;

    // Scope SD card operations with mutex
    {
      SpiBusMutex::Guard guard;
      FsFile file = root.openNextFile();
      if (!file) {
        break;
      }

      char name[500];
      file.getName(name, sizeof(name));
      auto fileName = String(name);

      // Skip hidden items (starting with ".")
      shouldHide = fileName.startsWith(".");

      // Check against explicitly hidden items list
      if (!shouldHide) {
        for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
          if (fileName.equals(HIDDEN_ITEMS[i])) {
            shouldHide = true;
            break;
          }
        }
      }

      if (!shouldHide) {
        info.name = fileName;
        info.isDirectory = file.isDirectory();

        if (info.isDirectory) {
          info.size = 0;
          info.isEpub = false;
        } else {
          info.size = file.size();
          info.isEpub = isEpubFile(info.name);
        }
      }
      file.close();
    }

    // Callback performs network operations - run without mutex
    if (!shouldHide) {
      callback(info);
    }

    yield();               // Yield to allow WiFi and other tasks to process during long scans
    esp_task_wdt_reset();  // Reset watchdog to prevent timeout on large directories
  }

  {
    SpiBusMutex::Guard guard;
    root.close();
  }
}

bool CrossPointWebServer::isEpubFile(const String& filename) const {
  String lower = filename;
  lower.toLowerCase();
  return lower.endsWith(".epub");
}

void CrossPointWebServer::handleFileList() const {
  sendPrecompressedHtml(server.get(), FilesPageHtml, FilesPageHtmlCompressedSize);
}

void CrossPointWebServer::handleFileListData() const {
  // Get current path from query string (default to root)
  String currentPath = "/";
  if (server->hasArg("path")) {
    const String rawArg = server->arg("path");
    currentPath = PathUtils::urlDecode(rawArg);
    LOG_DBG("WEB", "Files API - raw arg: '%s' (%d bytes), decoded: '%s' (%d bytes)", rawArg.c_str(),
            (int)rawArg.length(), currentPath.c_str(), (int)currentPath.length());

    // Validate path against traversal attacks
    if (!PathUtils::isValidSdPath(currentPath)) {
      LOG_WRN("WEB", "Path validation FAILED. raw='%s' (%d bytes) decoded='%s' (%d bytes)", rawArg.c_str(),
              (int)rawArg.length(), currentPath.c_str(), (int)currentPath.length());
      server->send(400, "text/plain", "Invalid path");
      return;
    }
    LOG_DBG("WEB", "Path validation OK");

    currentPath = PathUtils::normalizePath(currentPath);
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanFiles(currentPath.c_str(), [this, &output, &doc, &seenFirst](const FileInfo& info) {
    doc.clear();
    doc["name"] = info.name;
    doc["size"] = info.size;
    doc["isDirectory"] = info.isDirectory;
    doc["isEpub"] = info.isEpub;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      // JSON output truncated; skip this entry to avoid sending malformed JSON
      LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %s", info.name.c_str());
      return;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  });
  server->sendContent("]");
  // End of streamed response, empty chunk to signal client
  server->sendContent("");
  LOG_DBG("WEB", "Served file listing page for path: %s", currentPath.c_str());
}

void CrossPointWebServer::handleDownload() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  const String rawArg = server->arg("path");
  String itemPath = PathUtils::urlDecode(rawArg);
  if (!PathUtils::isValidSdPath(itemPath)) {
    LOG_WRN("WEB", "Download rejected - invalid path. raw='%s' decoded='%s'", rawArg.c_str(), itemPath.c_str());
    server->send(400, "text/plain", "Invalid path");
    return;
  }

  itemPath = PathUtils::normalizePath(itemPath);
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".")) {
    server->send(403, "text/plain", "Cannot access system files");
    return;
  }
  for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
    if (itemName.equals(HIDDEN_ITEMS[i])) {
      server->send(403, "text/plain", "Cannot access protected items");
      return;
    }
  }

  bool exists = false;
  {
    SpiBusMutex::Guard guard;
    exists = Storage.exists(itemPath.c_str());
  }
  if (!exists) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  FsFile file;
  {
    SpiBusMutex::Guard guard;
    file = Storage.open(itemPath.c_str());
  }
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  bool isDirectory = false;
  {
    SpiBusMutex::Guard guard;
    isDirectory = file.isDirectory();
  }
  if (isDirectory) {
    SpiBusMutex::Guard guard;
    file.close();
    server->send(400, "text/plain", "Path is a directory");
    return;
  }

  String contentType = "application/octet-stream";
  if (isEpubFile(itemPath)) {
    contentType = "application/epub+zip";
  }

  size_t fileSize = 0;
  {
    SpiBusMutex::Guard guard;
    fileSize = file.size();
  }

  char nameBuf[128] = {0};
  String filename = "download";
  {
    SpiBusMutex::Guard guard;
    if (file.getName(nameBuf, sizeof(nameBuf))) {
      filename = nameBuf;
    }
  }

  server->setContentLength(fileSize);
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server->send(200, contentType.c_str(), "");

  WiFiClient client = server->client();
  uint8_t buffer[1024];
  while (true) {
    size_t bytesRead = 0;
    {
      SpiBusMutex::Guard guard;
      bytesRead = file.read(buffer, sizeof(buffer));
    }
    if (bytesRead == 0) {
      break;
    }
    const size_t bytesWritten = client.write(buffer, bytesRead);
    if (bytesWritten != bytesRead) {
      LOG_WRN("WEB", "Download truncated for %s (wanted %u, wrote %u)", itemPath.c_str(),
              static_cast<unsigned int>(bytesRead), static_cast<unsigned int>(bytesWritten));
      break;
    }
    yield();
    esp_task_wdt_reset();
  }
  {
    SpiBusMutex::Guard guard;
    file.close();
  }
}

// Static variables for upload handling
static FsFile uploadFile;
static String uploadFileName;
static String uploadPath = "/";
static size_t uploadSize = 0;
static bool uploadSuccess = false;
static String uploadError = "";

// Upload write buffer - batches small writes into larger SD card operations
// 4KB is a good balance: large enough to reduce syscall overhead, small enough
// to keep individual write times short and avoid watchdog issues
constexpr size_t UPLOAD_BUFFER_SIZE = 4096;  // 4KB buffer
static uint8_t uploadBuffer[UPLOAD_BUFFER_SIZE];
static size_t uploadBufferPos = 0;

// Diagnostic counters for upload performance analysis
static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static bool flushUploadBuffer() {
  if (uploadBufferPos > 0 && uploadFile) {
    SpiBusMutex::Guard guard;
    esp_task_wdt_reset();  // Reset watchdog before potentially slow SD write
    const unsigned long writeStart = millis();
    const size_t written = uploadFile.write(uploadBuffer, uploadBufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    esp_task_wdt_reset();  // Reset watchdog after SD write

    if (written != uploadBufferPos) {
      LOG_DBG("WEB", "[UPLOAD] Buffer flush failed: expected %d, wrote %d", uploadBufferPos, written);
      uploadBufferPos = 0;
      return false;
    }
    uploadBufferPos = 0;
  }
  return true;
}

void CrossPointWebServer::handleUpload() const {
  static size_t lastLoggedSize = 0;

  // Reset watchdog at start of every upload callback - HTTP parsing can be slow
  esp_task_wdt_reset();

  // Safety check: ensure server is still valid
  if (!running || !server) {
    LOG_DBG("WEB", "[UPLOAD] ERROR: handleUpload called but server not running!");
    return;
  }

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    // Reset watchdog - this is the critical 1% crash point
    esp_task_wdt_reset();

    uploadFileName = upload.filename;
    uploadSize = 0;
    uploadSuccess = false;
    uploadError = "";
    uploadStartTime = millis();
    lastLoggedSize = 0;
    uploadBufferPos = 0;
    totalWriteTime = 0;
    writeCount = 0;

    // Validate filename to prevent path traversal
    if (!PathUtils::isValidFilename(uploadFileName)) {
      uploadError = "Invalid filename";
      LOG_WRN("WEB", "[UPLOAD] Invalid filename rejected: %s", uploadFileName.c_str());
      return;
    }

    // Get upload path from query parameter (defaults to root if not specified)
    // Note: We use query parameter instead of form data because multipart form
    // fields aren't available until after file upload completes
    if (server->hasArg("path")) {
      uploadPath = PathUtils::urlDecode(server->arg("path"));

      // Validate path against traversal attacks
      if (!PathUtils::isValidSdPath(uploadPath)) {
        uploadError = "Invalid path";
        LOG_WRN("WEB", "[UPLOAD] Path validation failed: %s", uploadPath.c_str());
        return;
      }

      uploadPath = PathUtils::normalizePath(uploadPath);
    } else {
      uploadPath = "/";
    }

    LOG_DBG("WEB", "[UPLOAD] START: %s to path: %s", uploadFileName.c_str(), uploadPath.c_str());
    LOG_DBG("WEB", "[UPLOAD] Free heap: %d bytes", ESP.getFreeHeap());

    // Create file path
    String filePath = uploadPath;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += uploadFileName;

    // Check if file already exists - SD operations can be slow
    esp_task_wdt_reset();
    if (Storage.exists(filePath.c_str())) {
      LOG_DBG("WEB", "[UPLOAD] Overwriting existing file: %s", filePath.c_str());
      esp_task_wdt_reset();
      Storage.remove(filePath.c_str());
    }

    // Open file for writing - this can be slow due to FAT cluster allocation
    esp_task_wdt_reset();
    if (!Storage.openFileForWrite("WEB", filePath, uploadFile)) {
      uploadError = "Failed to create file on SD card";
      LOG_DBG("WEB", "[UPLOAD] FAILED to create file: %s", filePath.c_str());
      return;
    }
    esp_task_wdt_reset();

    LOG_DBG("WEB", "[UPLOAD] File created successfully: %s", filePath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadError.isEmpty()) {
      // Buffer incoming data and flush when buffer is full
      // This reduces SD card write operations and improves throughput
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UPLOAD_BUFFER_SIZE - uploadBufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(uploadBuffer + uploadBufferPos, data, toCopy);
        uploadBufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        // Flush buffer when full
        if (uploadBufferPos >= UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer()) {
            uploadError = "Failed to write to SD card - disk may be full";
            {
              SpiBusMutex::Guard guard;
              uploadFile.close();
            }
            return;
          }
        }
      }

      uploadSize += upload.currentSize;

      // Log progress every 100KB
      if (uploadSize - lastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (uploadSize / 1024.0) / (elapsed / 1000.0) : 0;
        LOG_DBG("WEB", "[UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes", uploadSize, uploadSize / 1024.0, kbps,
                writeCount);
        lastLoggedSize = uploadSize;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      // Flush any remaining buffered data
      if (!flushUploadBuffer()) {
        uploadError = "Failed to write final data to SD card";
      }
      {
        SpiBusMutex::Guard guard;
        uploadFile.close();
      }

      if (uploadError.isEmpty()) {
        uploadSuccess = true;
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (uploadSize / 1024.0) / (elapsed / 1000.0) : 0;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0 / elapsed) : 0;
        LOG_DBG("WEB", "[UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)", uploadFileName.c_str(), uploadSize,
                elapsed, avgKbps);
        LOG_DBG("WEB", "[UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)", writeCount, totalWriteTime,
                writePercent);

        // Clear epub cache to prevent stale metadata issues when overwriting files
        String filePath = uploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += uploadFileName;
        clearEpubCacheIfNeeded(filePath);
        invalidateSleepCacheIfNeeded(filePath);
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadBufferPos = 0;  // Discard buffered data
    if (uploadFile) {
      SpiBusMutex::Guard guard;
      uploadFile.close();
      // Try to delete the incomplete file
      String filePath = uploadPath;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += uploadFileName;
      Storage.remove(filePath.c_str());
    }
    uploadError = "Upload aborted";
    LOG_DBG("WEB", "Upload aborted");
  }
}

void CrossPointWebServer::handleUploadPost() const {
  if (uploadSuccess) {
    server->send(200, "text/plain", "File uploaded successfully: " + uploadFileName);
  } else {
    const String error = uploadError.isEmpty() ? "Unknown error during upload" : uploadError;
    server->send(400, "text/plain", error);
  }
}

void CrossPointWebServer::handleCreateFolder() const {
  // Get folder name from form data
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }

  const String folderName = server->arg("name");

  // Validate folder name (no path separators or traversal)
  if (!PathUtils::isValidFilename(folderName)) {
    LOG_WRN("WEB", "Invalid folder name rejected: %s", folderName.c_str());
    server->send(400, "text/plain", "Invalid folder name");
    return;
  }

  // Get parent path
  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = PathUtils::urlDecode(server->arg("path"));

    // Validate path against traversal attacks
    if (!PathUtils::isValidSdPath(parentPath)) {
      LOG_WRN("WEB", "Path validation failed for mkdir: %s", parentPath.c_str());
      server->send(400, "text/plain", "Invalid path");
      return;
    }

    parentPath = PathUtils::normalizePath(parentPath);
  }

  // Build full folder path
  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  LOG_DBG("WEB", "Creating folder: %s", folderPath.c_str());

  // Check if already exists
  bool folderExists = false;
  {
    SpiBusMutex::Guard guard;
    folderExists = Storage.exists(folderPath.c_str());
  }
  if (folderExists) {
    server->send(400, "text/plain", "Folder already exists");
    return;
  }

  // Create the folder
  if (Storage.mkdir(folderPath.c_str())) {
    LOG_DBG("WEB", "Folder created successfully: %s", folderPath.c_str());
    server->send(200, "text/plain", "Folder created: " + folderName);
  } else {
    LOG_DBG("WEB", "Failed to create folder: %s", folderPath.c_str());
    server->send(500, "text/plain", "Failed to create folder");
  }
}

void CrossPointWebServer::handleRename() const {
  if (!server->hasArg("path") || !server->hasArg("name")) {
    server->send(400, "text/plain", "Missing path or new name");
    return;
  }

  String itemPath = PathUtils::urlDecode(server->arg("path"));
  String newName = server->arg("name");
  newName.trim();

  if (!PathUtils::isValidSdPath(itemPath)) {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  itemPath = PathUtils::normalizePath(itemPath);

  if (itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (newName.isEmpty()) {
    server->send(400, "text/plain", "New name cannot be empty");
    return;
  }
  if (newName.indexOf('/') >= 0 || newName.indexOf('\\') >= 0) {
    server->send(400, "text/plain", "Invalid file name");
    return;
  }
  if (newName.startsWith(".")) {
    server->send(403, "text/plain", "Cannot rename to hidden name");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".")) {
    server->send(403, "text/plain", "Cannot rename protected item");
    return;
  }
  if (newName == itemName) {
    server->send(200, "text/plain", "Name unchanged");
    return;
  }

  {
    SpiBusMutex::Guard guard;
    if (!Storage.exists(itemPath.c_str())) {
      server->send(404, "text/plain", "Item not found");
      return;
    }
  }

  FsFile file;
  {
    SpiBusMutex::Guard guard;
    file = Storage.open(itemPath.c_str());
  }
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  {
    SpiBusMutex::Guard guard;
    if (file.isDirectory()) {
      file.close();
      server->send(400, "text/plain", "Only files can be renamed");
      return;
    }
  }

  String parentPath = itemPath.substring(0, itemPath.lastIndexOf('/'));
  if (parentPath.isEmpty()) {
    parentPath = "/";
  }
  String newPath = parentPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += newName;

  bool targetExists = false;
  {
    SpiBusMutex::Guard guard;
    targetExists = Storage.exists(newPath.c_str());
  }
  if (targetExists) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  clearEpubCacheIfNeeded(itemPath);
  invalidateSleepCacheIfNeeded(itemPath);
  bool success = false;
  {
    SpiBusMutex::Guard guard;
    success = file.rename(newPath.c_str());
  }
  file.close();

  if (success) {
    LOG_DBG("WEB", "Renamed file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Renamed successfully");
  } else {
    LOG_ERR("WEB", "Failed to rename file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to rename file");
  }
}

void CrossPointWebServer::handleMove() const {
  if (!server->hasArg("path") || !server->hasArg("dest")) {
    server->send(400, "text/plain", "Missing path or destination");
    return;
  }

  String itemPath = PathUtils::urlDecode(server->arg("path"));
  String destPath = PathUtils::urlDecode(server->arg("dest"));

  if (!PathUtils::isValidSdPath(itemPath) || !PathUtils::isValidSdPath(destPath)) {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  itemPath = PathUtils::normalizePath(itemPath);
  destPath = PathUtils::normalizePath(destPath);

  if (itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".")) {
    server->send(403, "text/plain", "Cannot move protected item");
    return;
  }

  {
    SpiBusMutex::Guard guard;
    if (!Storage.exists(itemPath.c_str())) {
      server->send(404, "text/plain", "Item not found");
      return;
    }
  }

  FsFile file;
  {
    SpiBusMutex::Guard guard;
    file = Storage.open(itemPath.c_str());
  }
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  {
    SpiBusMutex::Guard guard;
    if (file.isDirectory()) {
      file.close();
      server->send(400, "text/plain", "Only files can be moved");
      return;
    }
  }

  bool destExists = false;
  {
    SpiBusMutex::Guard guard;
    destExists = Storage.exists(destPath.c_str());
  }
  if (!destExists) {
    file.close();
    server->send(404, "text/plain", "Destination not found");
    return;
  }
  FsFile destDir;
  {
    SpiBusMutex::Guard guard;
    destDir = Storage.open(destPath.c_str());
  }
  if (!destDir || !destDir.isDirectory()) {
    if (destDir) destDir.close();
    file.close();
    server->send(400, "text/plain", "Destination is not a folder");
    return;
  }
  destDir.close();

  String newPath = destPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += itemName;

  if (newPath == itemPath) {
    file.close();
    server->send(200, "text/plain", "Already in destination");
    return;
  }
  bool targetExists = false;
  {
    SpiBusMutex::Guard guard;
    targetExists = Storage.exists(newPath.c_str());
  }
  if (targetExists) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  clearEpubCacheIfNeeded(itemPath);
  invalidateSleepCacheIfNeeded(itemPath);
  bool success = false;
  {
    SpiBusMutex::Guard guard;
    success = file.rename(newPath.c_str());
  }
  file.close();

  if (success) {
    LOG_DBG("WEB", "Moved file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Moved successfully");
  } else {
    LOG_ERR("WEB", "Failed to move file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to move file");
  }
}

void CrossPointWebServer::handleDelete() const {
  // Get path from form data
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = PathUtils::urlDecode(server->arg("path"));
  const String itemType = server->hasArg("type") ? server->arg("type") : "file";

  // Validate path against traversal attacks
  if (!PathUtils::isValidSdPath(itemPath)) {
    LOG_WRN("WEB", "Path validation failed for delete: %s", itemPath.c_str());
    server->send(400, "text/plain", "Invalid path");
    return;
  }

  // Normalize before root checks so variants like "//" are treated as root.
  itemPath = PathUtils::normalizePath(itemPath);

  // Validate path
  if (itemPath == "/") {
    server->send(400, "text/plain", "Cannot delete root directory");
    return;
  }

  // Security check: prevent deletion of protected items
  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);

  // Check if item starts with a dot (hidden/system file)
  if (itemName.startsWith(".")) {
    LOG_DBG("WEB", "Delete rejected - hidden/system item: %s", itemPath.c_str());
    server->send(403, "text/plain", "Cannot delete system files");
    return;
  }

  // Check against explicitly protected items
  for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
    if (itemName.equals(HIDDEN_ITEMS[i])) {
      LOG_DBG("WEB", "Delete rejected - protected item: %s", itemPath.c_str());
      server->send(403, "text/plain", "Cannot delete protected items");
      return;
    }
  }

  // Check if item exists
  if (!Storage.exists(itemPath.c_str())) {
    LOG_DBG("WEB", "Delete failed - item not found: %s", itemPath.c_str());
    server->send(404, "text/plain", "Item not found");
    return;
  }

  LOG_DBG("WEB", "Attempting to delete %s: %s", itemType.c_str(), itemPath.c_str());

  bool success = false;

  if (itemType == "folder") {
    // For folders, try to remove (will fail if not empty)
    {
      SpiBusMutex::Guard guard;
      FsFile dir = Storage.open(itemPath.c_str());
      if (dir && dir.isDirectory()) {
        // Check if folder is empty
        FsFile entry = dir.openNextFile();
        if (entry) {
          // Folder is not empty
          entry.close();
          dir.close();
          LOG_WRN("WEB", "Delete failed - folder not empty: %s", itemPath.c_str());
          server->send(400, "text/plain", "Folder is not empty. Delete contents first.");
          return;
        }
        dir.close();
      }
    }
    {
      SpiBusMutex::Guard guard;
      success = Storage.rmdir(itemPath.c_str());
    }
  } else {
    // For files, use remove
    {
      SpiBusMutex::Guard guard;
      success = Storage.remove(itemPath.c_str());
    }
  }

  if (success) {
    LOG_DBG("WEB", "Successfully deleted: %s", itemPath.c_str());
    server->send(200, "text/plain", "Deleted successfully");
  } else {
    LOG_ERR("WEB", "Failed to delete: %s", itemPath.c_str());
    server->send(500, "text/plain", "Failed to delete item");
  }
}

void CrossPointWebServer::handleSettingsPage() const {
  sendPrecompressedHtml(server.get(), SettingsPageHtml, SettingsPageHtmlCompressedSize);
  LOG_DBG("WEB", "Served settings page");
}

void CrossPointWebServer::handlePokedexPluginPage() const {
#if ENABLE_WEB_POKEDEX_PLUGIN
  sendPrecompressedHtml(server.get(), PokedexPluginPageHtml, PokedexPluginPageHtmlCompressedSize);
  LOG_DBG("WEB", "Served pokedex plugin page");
#else
  server->send(404, "text/plain", "Pokedex plugin not enabled in this build");
#endif
}

void CrossPointWebServer::handleGetSettings() const {
  auto settings = getSettingsList();

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  for (const auto& s : settings) {
    if (!s.key) continue;  // Skip ACTION-only entries

    doc.clear();
    doc["key"] = s.key;
    doc["name"] = s.name;
    doc["category"] = s.category;

    switch (s.type) {
      case SettingType::TOGGLE: {
        doc["type"] = "toggle";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        }
        break;
      }
      case SettingType::ENUM: {
        doc["type"] = "enum";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        } else if (s.valueGetter) {
          doc["value"] = static_cast<int>(s.valueGetter());
        }
        JsonArray options = doc["options"].to<JsonArray>();
        for (const auto& opt : s.enumValues) {
          options.add(opt);
        }
        break;
      }
      case SettingType::VALUE: {
        doc["type"] = "value";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        }
        doc["min"] = s.valueRange.min;
        doc["max"] = s.valueRange.max;
        doc["step"] = s.valueRange.step;
        break;
      }
      case SettingType::STRING: {
        doc["type"] = "string";
        const bool isPasswordField = (s.key && (strstr(s.key, "password") != nullptr || strstr(s.key, "Password"))) ||
                                     (s.name && strstr(s.name, "Password"));
        if (isPasswordField) {
          // Do not expose stored passwords over the settings API.
          doc["value"] = "";
          if (s.stringGetter) {
            doc["hasValue"] = !s.stringGetter().empty();
          } else if (s.stringPtr) {
            doc["hasValue"] = s.stringPtr[0] != '\0';
          } else {
            doc["hasValue"] = false;
          }
        } else if (s.stringGetter) {
          doc["value"] = s.stringGetter();
        } else if (s.stringPtr) {
          doc["value"] = s.stringPtr;
        }
        break;
      }
      default:
        continue;
    }

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      LOG_DBG("WEB", "Skipping oversized setting JSON for: %s", s.key);
      continue;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served settings API");
}

void CrossPointWebServer::handlePostSettings() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  auto settings = getSettingsList();
  int applied = 0;

  for (auto& s : settings) {
    if (!s.key) continue;
    if (!doc[s.key].is<JsonVariant>()) continue;

    switch (s.type) {
      case SettingType::TOGGLE: {
        const int val = doc[s.key].as<int>() ? 1 : 0;
        if (s.valuePtr) {
          SETTINGS.*(s.valuePtr) = val;
        }
        applied++;
        break;
      }
      case SettingType::ENUM: {
        const int val = doc[s.key].as<int>();
        if (val >= 0 && val < static_cast<int>(s.enumValues.size())) {
          if (s.valuePtr) {
            if (s.valuePtr == &CrossPointSettings::frontButtonLayout) {
              SETTINGS.applyFrontButtonLayoutPreset(static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(val));
            } else {
              SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
            }
          } else if (s.valueSetter) {
            s.valueSetter(static_cast<uint8_t>(val));
          }
          applied++;
        }
        break;
      }
      case SettingType::VALUE: {
        const int val = doc[s.key].as<int>();
        if (val >= s.valueRange.min && val <= s.valueRange.max) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          }
          applied++;
        }
        break;
      }
      case SettingType::STRING: {
        const std::string val = doc[s.key].as<std::string>();
        if (s.stringSetter) {
          s.stringSetter(val);
        } else if (s.stringPtr && s.stringMaxLen > 0) {
          strncpy(s.stringPtr, val.c_str(), s.stringMaxLen - 1);
          s.stringPtr[s.stringMaxLen - 1] = '\0';
        }
        applied++;
        break;
      }
      default:
        break;
    }
  }

  SETTINGS.enforceButtonLayoutConstraints();
  SETTINGS.saveToFile();

  LOG_DBG("WEB", "Applied %d setting(s)", applied);
  server->send(200, "text/plain", String("Applied ") + String(applied) + " setting(s)");
}

void CrossPointWebServer::handleRecentBooks() const {
  const auto& books = RECENT_BOOKS.getBooks();

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  for (const auto& book : books) {
    doc.clear();
    doc["path"] = book.path;
    doc["title"] = book.title;
    doc["author"] = book.author;
    doc["hasCover"] = !book.coverBmpPath.empty();

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
}

void CrossPointWebServer::handleCover() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  const String rawArg = server->arg("path");
  String bookPath = PathUtils::urlDecode(rawArg);

  if (!PathUtils::isValidSdPath(bookPath)) {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  bookPath = PathUtils::normalizePath(bookPath);

  // Look up the book in recent books to get the cached cover path
  const auto& books = RECENT_BOOKS.getBooks();
  std::string coverPath;

  for (const auto& book : books) {
    if (book.path == bookPath.c_str()) {
      coverPath = book.coverBmpPath;
      break;
    }
  }

  // If not found in recent books or no cover, try generating one
  if (coverPath.empty()) {
    String lower = bookPath;
    lower.toLowerCase();
#if ENABLE_EPUB_SUPPORT
    if (lower.endsWith(".epub")) {
      Epub epub(bookPath.c_str(), "/.crosspoint");
      SpiBusMutex::Guard guard;
      if (epub.load(false)) {
        coverPath = epub.getThumbBmpPath();
      }
#else
    if (false) {
#endif
    }
  }

  if (coverPath.empty()) {
    server->send(404, "text/plain", "No cover available");
    return;
  }

  // Stream the BMP file to the client
  bool exists = false;
  {
    SpiBusMutex::Guard guard;
    exists = Storage.exists(coverPath.c_str());
  }
  if (!exists) {
    server->send(404, "text/plain", "Cover file not found");
    return;
  }

  FsFile file;
  {
    SpiBusMutex::Guard guard;
    file = Storage.open(coverPath.c_str());
  }
  if (!file) {
    server->send(500, "text/plain", "Failed to open cover");
    return;
  }

  size_t fileSize = 0;
  {
    SpiBusMutex::Guard guard;
    fileSize = file.size();
  }

  server->setContentLength(fileSize);
  server->sendHeader("Cache-Control", "public, max-age=3600");
  server->send(200, "image/bmp", "");

  WiFiClient client = server->client();
  uint8_t buffer[1024];
  while (true) {
    size_t bytesRead = 0;
    {
      SpiBusMutex::Guard guard;
      bytesRead = file.read(buffer, sizeof(buffer));
    }
    if (bytesRead == 0) break;
    client.write(buffer, bytesRead);
    yield();
    esp_task_wdt_reset();
  }
  {
    SpiBusMutex::Guard guard;
    file.close();
  }
}

// WebSocket callback trampoline
void CrossPointWebServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}

// WebSocket event handler for fast binary uploads
// Protocol:
//   1. Client sends TEXT message: "START:<filename>:<size>:<path>"
//   2. Client sends BINARY messages with file data chunks
//   3. Server sends TEXT "PROGRESS:<received>:<total>" after each chunk
//   4. Server sends TEXT "DONE" or "ERROR:<message>" when complete
void CrossPointWebServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      LOG_DBG("WS", "Client %u disconnected", num);
      // Clean up any in-progress upload
      if (wsUploadInProgress && wsUploadFile) {
        SpiBusMutex::Guard guard;
        wsUploadFile.close();
        // Delete incomplete file
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        Storage.remove(filePath.c_str());
        LOG_DBG("WS", "Deleted incomplete upload: %s", filePath.c_str());
      }
      // Reset all upload state to prevent stale data affecting next connection
      wsUploadInProgress = false;
      wsUploadFileName.clear();
      wsUploadPath.clear();
      wsUploadSize = 0;
      wsUploadReceived = 0;
      wsLastProgressSent = 0;
      wsUploadStartTime = 0;
      break;

    case WStype_CONNECTED: {
      LOG_DBG("WS", "Client %u connected", num);
      break;
    }

    case WStype_TEXT: {
      // Parse control messages
      String msg = String((char*)payload);
      LOG_DBG("WS", "Text from client %u: %s", num, msg.c_str());

      if (msg.startsWith("START:")) {
        // Parse: START:<filename>:<size>:<path> (filename/path URL-encoded)
        int firstColon = msg.indexOf(':', 6);
        int secondColon = msg.indexOf(':', firstColon + 1);

        if (firstColon > 0 && secondColon > 0) {
          wsUploadFileName = PathUtils::urlDecode(msg.substring(6, firstColon));
          wsUploadSize = msg.substring(firstColon + 1, secondColon).toInt();
          wsUploadPath = PathUtils::urlDecode(msg.substring(secondColon + 1));
          wsUploadReceived = 0;
          wsLastProgressSent = 0;
          wsUploadStartTime = millis();

          // Validate filename against traversal attacks
          if (!PathUtils::isValidFilename(wsUploadFileName)) {
            LOG_WRN("WS", "Invalid filename rejected: %s", wsUploadFileName.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid filename");
            return;
          }

          // Validate path against traversal attacks
          if (!PathUtils::isValidSdPath(wsUploadPath)) {
            LOG_WRN("WS", "Path validation failed: %s", wsUploadPath.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid path");
            return;
          }

          wsUploadPath = PathUtils::normalizePath(wsUploadPath);

          // Build file path
          String filePath = wsUploadPath;
          if (!filePath.endsWith("/")) filePath += "/";
          filePath += wsUploadFileName;

          LOG_DBG("WS", "Starting upload: %s (%d bytes) to %s", wsUploadFileName.c_str(), wsUploadSize,
                  filePath.c_str());

          // Check if file exists and remove it
          esp_task_wdt_reset();
          {
            SpiBusMutex::Guard guard;
            if (Storage.exists(filePath.c_str())) {
              Storage.remove(filePath.c_str());
            }
          }

          // Open file for writing
          esp_task_wdt_reset();
          {
            SpiBusMutex::Guard guard;
            if (!Storage.openFileForWrite("WS", filePath, wsUploadFile)) {
              wsServer->sendTXT(num, "ERROR:Failed to create file");
              wsUploadInProgress = false;
              return;
            }
          }
          esp_task_wdt_reset();

          wsUploadInProgress = true;
          wsServer->sendTXT(num, "READY");
        } else {
          wsServer->sendTXT(num, "ERROR:Invalid START format");
        }
      }
      break;
    }

    case WStype_BIN: {
      if (!wsUploadInProgress || !wsUploadFile) {
        wsServer->sendTXT(num, "ERROR:No upload in progress");
        return;
      }

      // Write binary data directly to file
      esp_task_wdt_reset();
      size_t written = 0;
      {
        SpiBusMutex::Guard guard;
        written = wsUploadFile.write(payload, length);
      }
      esp_task_wdt_reset();

      if (written != length) {
        SpiBusMutex::Guard guard;
        wsUploadFile.close();
        wsUploadInProgress = false;
        wsServer->sendTXT(num, "ERROR:Write failed - disk full?");
        return;
      }

      wsUploadReceived += written;

      // Send progress update (every 64KB or at end)
      if (wsUploadReceived - wsLastProgressSent >= 65536 || wsUploadReceived >= wsUploadSize) {
        String progress = "PROGRESS:" + String(wsUploadReceived) + ":" + String(wsUploadSize);
        wsServer->sendTXT(num, progress);
        wsLastProgressSent = wsUploadReceived;
      }

      // Check if upload complete
      if (wsUploadReceived >= wsUploadSize) {
        {
          SpiBusMutex::Guard guard;
          wsUploadFile.close();
        }
        wsUploadInProgress = false;

        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = wsUploadSize;
        wsLastCompleteAt = millis();

        unsigned long elapsed = millis() - wsUploadStartTime;
        float kbps = (elapsed > 0) ? (wsUploadSize / 1024.0) / (elapsed / 1000.0) : 0;

        LOG_DBG("WS", "Upload complete: %s (%d bytes in %lu ms, %.1f KB/s)", wsUploadFileName.c_str(), wsUploadSize,
                elapsed, kbps);

        // Clear caches to prevent stale data when overwriting files
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        clearEpubCacheIfNeeded(filePath);
        invalidateSleepCacheIfNeeded(filePath);

        wsServer->sendTXT(num, "DONE");
        wsLastProgressSent = 0;
      }
      break;
    }

    default:
      break;
  }
}

#include "WifiCredentialStore.h"

#if ENABLE_WEB_WIFI_SETUP
void CrossPointWebServer::handleWifiScan() const {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();

  for (int i = 0; i < n; ++i) {
    JsonObject obj = array.add<JsonObject>();
    obj["ssid"] = WiFi.SSID(i);
    obj["rssi"] = WiFi.RSSI(i);
    obj["encrypted"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    obj["saved"] = WIFI_STORE.hasSavedCredential(WiFi.SSID(i).c_str());
  }
  WiFi.scanDelete();

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleWifiConnect() const {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing body");
    return;
  }
  JsonDocument doc;
  deserializeJson(doc, server->arg("plain"));

  String ssid = doc["ssid"];
  String password = doc["password"];

  if (ssid.length() == 0) {
    server->send(400, "text/plain", "SSID required");
    return;
  }

  WIFI_STORE.addCredential(ssid.c_str(), password.c_str());
  WIFI_STORE.saveToFile();

  server->send(200, "text/plain", "WiFi credentials saved");
}

void CrossPointWebServer::handleWifiForget() const {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing body");
    return;
  }
  JsonDocument doc;
  deserializeJson(doc, server->arg("plain"));
  String ssid = doc["ssid"];

  if (ssid.length() > 0) {
    WIFI_STORE.removeCredential(ssid.c_str());
    WIFI_STORE.saveToFile();
    server->send(200, "text/plain", "WiFi credentials removed");
  } else {
    server->send(400, "text/plain", "SSID required");
  }
}
#endif
