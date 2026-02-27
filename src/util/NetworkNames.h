#pragma once

#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"

// Helpers for building device-specific network names from the user-configured
// deviceName setting. All strings fall back to the last 4 hex digits of the
// MAC address when deviceName is empty.
namespace NetworkNames {

// mDNS hostname: "crosspoint-{name}" or "crosspoint-{mac4}"
inline void getDeviceHostname(char* buf, size_t size) {
  if (strlen(SETTINGS.deviceName) > 0) {
    snprintf(buf, size, "crosspoint-%s", SETTINGS.deviceName);
  } else {
    uint8_t mac[6] = {};
    WiFi.macAddress(mac);
    snprintf(buf, size, "crosspoint-%02x%02x", mac[4], mac[5]);
  }
}

// DHCP hostname seeded to the router: "X4-{name}" or "X4-{mac4}"
inline void getDhcpHostname(char* buf, size_t size) {
  if (strlen(SETTINGS.deviceName) > 0) {
    snprintf(buf, size, "X4-%s", SETTINGS.deviceName);
  } else {
    uint8_t mac[6] = {};
    WiFi.macAddress(mac);
    snprintf(buf, size, "X4-%02x%02x", mac[4], mac[5]);
  }
}

// Access-Point SSID: "CrossPoint-{name}" or "CrossPoint-Reader"
inline void getApSsid(char* buf, size_t size) {
  if (strlen(SETTINGS.deviceName) > 0) {
    snprintf(buf, size, "CrossPoint-%s", SETTINGS.deviceName);
  } else {
    strncpy(buf, "CrossPoint-Reader", size);
    buf[size - 1] = '\0';
  }
}

}  // namespace NetworkNames
