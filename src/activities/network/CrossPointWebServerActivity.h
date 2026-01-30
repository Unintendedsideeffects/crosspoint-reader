#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "NetworkModeSelectionActivity.h"
#include "activities/ActivityWithSubactivity.h"
#include "network/CrossPointWebServer.h"

// Web server activity states
enum class WebServerActivityState {
  MODE_SELECTION,  // Choosing between Join Network, Calibre, and Create Hotspot
  WIFI_SELECTION,  // WiFi selection subactivity is active (for Join Network/Calibre mode)
  AP_STARTING,     // Starting Access Point mode
  SERVER_RUNNING,  // Web server is running and handling requests
  SHUTTING_DOWN    // Shutting down server and WiFi
};

/**
 * CrossPointWebServerActivity is the unified entry point for all file transfer functionality.
 * It handles:
 * - "Join a Network" (STA mode) - Connect to existing WiFi, show file transfer UI
 * - "Connect to Calibre" (STA mode) - Connect to existing WiFi, show Calibre-specific UI
 * - "Create Hotspot" (AP mode) - Create an Access Point, show file transfer UI
 *
 * This consolidates the previous separate CalibreConnectActivity into a single activity
 * with mode-specific UI rendering.
 */
class CrossPointWebServerActivity final : public ActivityWithSubactivity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  std::atomic<bool> exitTaskRequested{false};  // Signal for graceful task shutdown
  std::atomic<bool> taskHasExited{false};      // Confirmation that task exited
  bool updateRequired = false;
  WebServerActivityState state = WebServerActivityState::MODE_SELECTION;
  const std::function<void()> onGoBack;

  // Network mode
  NetworkMode networkMode = NetworkMode::JOIN_NETWORK;
  bool isApMode = false;

  // Web server - owned by this activity
  std::unique_ptr<CrossPointWebServer> webServer;

  // Server status
  std::string connectedIP;
  std::string connectedSSID;  // For STA mode: network name, For AP mode: AP name

  // Performance monitoring
  unsigned long lastHandleClientTime = 0;

  // Upload progress tracking (for Calibre mode UI)
  size_t lastProgressReceived = 0;
  size_t lastProgressTotal = 0;
  std::string currentUploadName;
  std::string lastCompleteName;
  unsigned long lastCompleteAt = 0;

  static void taskTrampoline(void* param);
  void displayTaskLoop();  // No longer [[noreturn]] - exits gracefully
  void render() const;
  void renderServerRunning() const;
  void renderCalibreUI() const;
  void renderFileTransferUI() const;

  void onNetworkModeSelected(NetworkMode mode);
  void onWifiSelectionComplete(bool connected);
  void startAccessPoint();
  void startWebServer();
  void stopWebServer();
  void updateUploadProgress();

 public:
  explicit CrossPointWebServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::function<void()>& onGoBack)
      : ActivityWithSubactivity("CrossPointWebServer", renderer, mappedInput), onGoBack(onGoBack) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
  bool blocksBackgroundServer() override { return true; }
};
