#pragma once

#include <SPIFFS.h>

#include <cstdint>

/// Manages the SPIFFS partition used for the active custom font slot.
/// On first boot (or after OEM firmware), formats and writes an ownership
/// marker so subsequent boots skip the expensive format step.
class HalSpiffs {
 public:
  static constexpr const char* OWNERSHIP_MARKER = "/.crosspoint_spiffs";
  static constexpr const char* FONT_DIR = "/font";
  static constexpr const char* FONT_JSON = "/font/font.json";

  /// Mount SPIFFS, format if foreign (no ownership marker), write marker.
  /// `maxOpenFiles`: passed to SPIFFS.begin; 8 is sufficient for concurrent reads.
  /// Returns true if SPIFFS is ready for use.
  static bool init(uint8_t maxOpenFiles = 8);

  /// True after a successful init().
  static bool ready() { return _ready; }

  /// Remove all files under FONT_DIR and the font.json commit marker.
  static void clearFontSlot();

 private:
  static bool _ready;

  static bool ensureOwnership();
};
