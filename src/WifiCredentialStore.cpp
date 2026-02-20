#include "WifiCredentialStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include "SpiBusMutex.h"
#include "network/BackgroundWebServer.h"

// Initialize the static instance
WifiCredentialStore WifiCredentialStore::instance;

namespace {
// File format version
constexpr uint8_t WIFI_FILE_VERSION = 1;

// WiFi credentials file path
constexpr char WIFI_FILE[] = "/.crosspoint/wifi.bin";

// Obfuscation key - "CrossPoint" in ASCII
// This is NOT cryptographic security, just prevents casual file reading
constexpr uint8_t OBFUSCATION_KEY[] = {0x43, 0x72, 0x6F, 0x73, 0x73, 0x50, 0x6F, 0x69, 0x6E, 0x74};
constexpr size_t KEY_LENGTH = sizeof(OBFUSCATION_KEY);
}  // namespace

void WifiCredentialStore::obfuscate(std::string& data) const {
  LOG_DBG("WCS", "Obfuscating/deobfuscating %zu bytes", data.size());
  for (size_t i = 0; i < data.size(); i++) {
    data[i] ^= OBFUSCATION_KEY[i % KEY_LENGTH];
  }
}

bool WifiCredentialStore::saveToFile() const {
  SpiBusMutex::Guard guard;
  // Make sure the directory exists
  Storage.mkdir("/.crosspoint");

  FsFile file;
  if (!Storage.openFileForWrite("WCS", WIFI_FILE, file)) {
    return false;
  }

  // Write header
  serialization::writePod(file, WIFI_FILE_VERSION);
  serialization::writePod(file, static_cast<uint8_t>(credentials.size()));

  // Write each credential
  for (const auto& cred : credentials) {
    // Write SSID (plaintext - not sensitive)
    serialization::writeString(file, cred.ssid);
    LOG_DBG("WCS", "Saving SSID: %s, password length: %zu", cred.ssid.c_str(), cred.password.size());

    // Write password (obfuscated)
    std::string obfuscatedPwd = cred.password;
    obfuscate(obfuscatedPwd);
    serialization::writeString(file, obfuscatedPwd);
  }

  file.close();
  LOG_INF("WCS", "Saved %zu WiFi credentials to file", credentials.size());
  return true;
}

bool WifiCredentialStore::loadFromFile() {
  SpiBusMutex::Guard guard;
  FsFile file;
  if (!Storage.openFileForRead("WCS", WIFI_FILE, file)) {
    return false;
  }

  // Read and verify version
  uint8_t version = 0;
  if (!serialization::readPod(file, version)) {
    LOG_ERR("WCS", "Failed to read file version");
    file.close();
    return false;
  }
  if (version != WIFI_FILE_VERSION) {
    LOG_ERR("WCS", "Unknown file version: %u", version);
    file.close();
    return false;
  }

  // Read credential count
  uint8_t count = 0;
  if (!serialization::readPod(file, count)) {
    LOG_ERR("WCS", "Failed to read credential count");
    file.close();
    return false;
  }

  // Read credentials
  credentials.clear();
  for (uint8_t i = 0; i < count && i < MAX_NETWORKS; i++) {
    WifiCredential cred;

    // Read SSID
    if (!serialization::readString(file, cred.ssid)) {
      LOG_ERR("WCS", "Failed to read SSID at index %u", i);
      file.close();
      return false;
    }

    // Read and deobfuscate password
    if (!serialization::readString(file, cred.password)) {
      LOG_ERR("WCS", "Failed to read password at index %u", i);
      file.close();
      return false;
    }
    LOG_DBG("WCS", "Loaded SSID: %s, obfuscated password length: %zu", cred.ssid.c_str(), cred.password.size());
    obfuscate(cred.password);  // XOR is symmetric, so same function deobfuscates
    LOG_DBG("WCS", "After deobfuscation, password length: %zu", cred.password.size());

    credentials.push_back(cred);
  }

  file.close();
  LOG_INF("WCS", "Loaded %zu WiFi credentials from file", credentials.size());
  return true;
}

bool WifiCredentialStore::addCredential(const std::string& ssid, const std::string& password) {
  // Check if this SSID already exists and update it
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    cred->password = password;
    LOG_INF("WCS", "Updated credentials for: %s", ssid.c_str());
    const bool saved = saveToFile();
    if (saved) {
      BackgroundWebServer::getInstance().invalidateCredentialsCache();
    }
    return saved;
  }

  // Check if we've reached the limit
  if (credentials.size() >= MAX_NETWORKS) {
    LOG_WRN("WCS", "Cannot add more networks, limit of %zu reached", MAX_NETWORKS);
    return false;
  }

  // Add new credential
  credentials.push_back({ssid, password});
  LOG_INF("WCS", "Added credentials for: %s", ssid.c_str());
  const bool saved = saveToFile();
  if (saved) {
    BackgroundWebServer::getInstance().invalidateCredentialsCache();
  }
  return saved;
}

bool WifiCredentialStore::removeCredential(const std::string& ssid) {
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    credentials.erase(cred);
    LOG_INF("WCS", "Removed credentials for: %s", ssid.c_str());
    const bool saved = saveToFile();
    if (saved) {
      BackgroundWebServer::getInstance().invalidateCredentialsCache();
    }
    return saved;
  }
  return false;  // Not found
}

const WifiCredential* WifiCredentialStore::findCredential(const std::string& ssid) const {
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });

  if (cred != credentials.end()) {
    return &*cred;
  }

  return nullptr;
}

bool WifiCredentialStore::hasSavedCredential(const std::string& ssid) const { return findCredential(ssid) != nullptr; }

void WifiCredentialStore::setLastConnectedSsid(const std::string& ssid) { lastConnectedSsid = ssid; }

const std::string& WifiCredentialStore::getLastConnectedSsid() const { return lastConnectedSsid; }

void WifiCredentialStore::clearLastConnectedSsid() { lastConnectedSsid.clear(); }

void WifiCredentialStore::clearAll() {
  credentials.clear();
  lastConnectedSsid.clear();
  if (saveToFile()) {
    BackgroundWebServer::getInstance().invalidateCredentialsCache();
  }
  LOG_INF("WCS", "Cleared all WiFi credentials");
}
