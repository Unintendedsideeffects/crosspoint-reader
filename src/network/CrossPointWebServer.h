#pragma once

#include <FeatureFlags.h>
#include <HalStorage.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiUdp.h>

#include <memory>
#include <string>
#include <vector>

// Structure to hold file information
struct FileInfo {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
};

class CrossPointWebServer {
 public:
  struct WsUploadStatus {
    bool inProgress = false;
    size_t received = 0;
    size_t total = 0;
    std::string filename;
    std::string lastCompleteName;
    size_t lastCompleteSize = 0;
    unsigned long lastCompleteAt = 0;
  };

  CrossPointWebServer();
  ~CrossPointWebServer();

  // Start the web server (call after WiFi is connected)
  void begin();

  // Stop the web server
  void stop();

  // Call this periodically to handle client requests
  void handleClient();

  // Check if server is running
  bool isRunning() const { return running; }

  WsUploadStatus getWsUploadStatus() const;

  // Get the port number
  uint16_t getPort() const { return port; }

 private:
  std::unique_ptr<WebServer> server = nullptr;
  std::unique_ptr<WebSocketsServer> wsServer = nullptr;
  bool running = false;
  bool apMode = false;  // true when running in AP mode, false for STA mode
  uint16_t port = 80;
  uint16_t wsPort = 81;  // WebSocket port
  WiFiUDP udp;
  bool udpActive = false;

  // WebSocket upload state
  void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  static void wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length);

  // File scanning
  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  String formatFileSize(size_t bytes) const;
  bool isEpubFile(const String& filename) const;

  // Request handlers
  void handleRoot() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handlePlugins() const;
  void handleTodoEntry();
  void handleFileList() const;
  void handleFileListData() const;
  void handleDownload() const;
  void handleUpload() const;
  void handleUploadPost() const;
  void handleCreateFolder() const;
  void handleRename() const;
  void handleMove() const;
  void handleDelete() const;

  // Settings handlers
  void handleSettingsPage() const;
  void handlePokedexPluginPage() const;
  void handleGetSettings() const;
  void handlePostSettings();
#if ENABLE_USER_FONTS
  void handleRescanUserFonts();
  void handleFontUpload();
  void handleFontUploadPost();
#endif

  // API handlers for web UI
  void handleRecentBooks() const;
  void handleCover() const;

  // WiFi handlers
#if ENABLE_WEB_WIFI_SETUP
  void handleWifiScan() const;
  void handleWifiConnect() const;
  void handleWifiForget() const;
#endif
};
