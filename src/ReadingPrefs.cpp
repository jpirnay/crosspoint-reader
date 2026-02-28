#include "ReadingPrefs.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

bool BookPrefs::loadFromFile(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    return false;
  }
  String json = Storage.readFile(path.c_str());
  if (json.isEmpty()) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return false;
  }
  embeddedStyle = doc["embeddedStyle"] | true;
  return true;
}

bool BookPrefs::saveToFile(const std::string& path) const {
  JsonDocument doc;
  doc["embeddedStyle"] = embeddedStyle;
  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path.c_str(), json);
}
