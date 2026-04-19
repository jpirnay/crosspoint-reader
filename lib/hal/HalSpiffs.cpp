#include "HalSpiffs.h"

#include <Logging.h>

bool HalSpiffs::_ready = false;

bool HalSpiffs::init(uint8_t maxOpenFiles) {
  _ready = false;

  if (!SPIFFS.begin(false, "/spiffs", maxOpenFiles)) {
    // Mount failed — format and retry once
    LOG_DBG("SPIFFS", "Mount failed; formatting");
    if (!SPIFFS.format()) {
      LOG_ERR("SPIFFS", "Format failed");
      return false;
    }
    if (!SPIFFS.begin(false, "/spiffs", maxOpenFiles)) {
      LOG_ERR("SPIFFS", "Mount after format failed");
      return false;
    }
  }

  if (!ensureOwnership()) {
    return false;
  }

  _ready = true;
  LOG_DBG("SPIFFS", "Ready (total=%u used=%u)", SPIFFS.totalBytes(), SPIFFS.usedBytes());
  return true;
}

bool HalSpiffs::ensureOwnership() {
  if (SPIFFS.exists(OWNERSHIP_MARKER)) {
    return true;  // already ours
  }

  // Foreign content present — wipe and reformat
  LOG_DBG("SPIFFS", "No ownership marker — formatting to reclaim partition");
  SPIFFS.end();
  if (!SPIFFS.format()) {
    LOG_ERR("SPIFFS", "Format failed during ownership claim");
    return false;
  }
  if (!SPIFFS.begin(false, "/spiffs", 8)) {
    LOG_ERR("SPIFFS", "Re-mount after ownership format failed");
    return false;
  }

  // Write marker
  File f = SPIFFS.open(OWNERSHIP_MARKER, FILE_WRITE);
  if (!f) {
    LOG_ERR("SPIFFS", "Failed to write ownership marker");
    return false;
  }
  f.print("crosspoint");
  f.close();
  LOG_DBG("SPIFFS", "Ownership marker written");
  return true;
}

void HalSpiffs::clearFontSlot() {
  if (!_ready) return;

  // Remove all files in FONT_DIR (SPIFFS has no recursive rmdir; enumerate and delete)
  File dir = SPIFFS.open(FONT_DIR);
  if (dir && dir.isDirectory()) {
    File entry = dir.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        const String path = entry.path();
        entry.close();
        SPIFFS.remove(path.c_str());
      } else {
        entry.close();
      }
      entry = dir.openNextFile();
    }
    dir.close();
  }
  // Remove the commit marker last so an interrupted clear is safe to retry
  SPIFFS.remove(FONT_JSON);
}
