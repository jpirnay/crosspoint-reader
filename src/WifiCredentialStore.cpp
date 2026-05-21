#include "WifiCredentialStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

// Initialize the static instance
WifiCredentialStore WifiCredentialStore::instance;

namespace {
constexpr char WIFI_FILE_JSON[] = "/.crosspoint/wifi.json";
}  // namespace

bool WifiCredentialStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveWifi(*this, WIFI_FILE_JSON);
}

bool WifiCredentialStore::loadFromFile() {
  if (Storage.exists(WIFI_FILE_JSON)) {
    String json = Storage.readFile(WIFI_FILE_JSON);
    if (!json.isEmpty()) {
      bool resave = false;
      bool result = JsonSettingsIO::loadWifi(*this, json.c_str(), &resave);
      if (result && resave) {
        LOG_DBG("WCS", "Resaving JSON with obfuscated passwords");
        saveToFile();
      }
      return result;
    }
  }
  return false;
}

bool WifiCredentialStore::addCredential(const std::string& ssid, const std::string& password) {
  // Check if this SSID already exists and update it
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    cred->password = password;
    LOG_DBG("WCS", "Updated credentials for: %s", ssid.c_str());
    return saveToFile();
  }

  // Check if we've reached the limit
  if (credentials.size() >= MAX_NETWORKS) {
    LOG_DBG("WCS", "Cannot add more networks, limit of %zu reached", MAX_NETWORKS);
    return false;
  }

  // Add new credential
  credentials.push_back({ssid, password});
  LOG_DBG("WCS", "Added credentials for: %s", ssid.c_str());
  return saveToFile();
}

bool WifiCredentialStore::removeCredential(const std::string& ssid) {
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    credentials.erase(cred);
    LOG_DBG("WCS", "Removed credentials for: %s", ssid.c_str());
    if (ssid == lastConnectedSsid) {
      clearLastConnectedSsid();
    }
    return saveToFile();
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

void WifiCredentialStore::setLastConnectedSsid(const std::string& ssid) {
  if (lastConnectedSsid != ssid) {
    lastConnectedSsid = ssid;
    saveToFile();
  }
}

const std::string& WifiCredentialStore::getLastConnectedSsid() const { return lastConnectedSsid; }

void WifiCredentialStore::clearLastConnectedSsid() {
  if (!lastConnectedSsid.empty()) {
    lastConnectedSsid.clear();
    saveToFile();
  }
}

void WifiCredentialStore::setLastKnownMacAddress(const std::string& mac) {
  if (lastKnownMacAddress != mac) {
    lastKnownMacAddress = mac;
    saveToFile();
  }
}

const std::string& WifiCredentialStore::getLastKnownMacAddress() const { return lastKnownMacAddress; }

void WifiCredentialStore::clearAll() {
  credentials.clear();
  lastConnectedSsid.clear();
  lastKnownMacAddress.clear();
  saveToFile();
  LOG_DBG("WCS", "Cleared all WiFi credentials");
}
