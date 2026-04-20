#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <IBitmapSource.h>

#include <cstdint>
#include <vector>

/// Loads a custom EPD font family from SPIFFS into heap (metrics only).
/// Bitmap data stays in SPIFFS and is streamed on demand via SpiffsBitmapSource.
///
/// Lifecycle:
///   1. discoverFamilies() — scan SD /fonts/ at boot
///   2. init()             — load Regular style from SPIFFS into heap
///   3. ensureStyle()      — lazily load Bold/Italic/BoldItalic on first use
///   4. registerWithRenderer() — wire into GfxRenderer font map
class CustomFontLoader {
 public:
  static constexpr size_t HEAP_GUARD_BYTES = 80 * 1024;

  static constexpr const char* SD_FONTS_DIR = "/fonts";
  static constexpr const char* SPIFFS_FONT_DIR = "/font";
  static constexpr const char* SPIFFS_FONT_JSON = "/font/font.json";
  static constexpr size_t MAX_NAME_LEN = 60;

  /// Validate a user-supplied font family name. Accepts only letters, digits,
  /// '-' and '_'; rejects empty or overlong names. Used as a defence-in-depth
  /// check before any path is constructed from the name (upload, activate,
  /// delete, SD scan echo). Keeps all attacker-controlled name handling in one
  /// place so we can't forget a gate on a new handler.
  static bool isValidName(const char* name);

  CustomFontLoader() = default;
  ~CustomFontLoader() { release(); }

  // Non-copyable
  CustomFontLoader(const CustomFontLoader&) = delete;
  CustomFontLoader& operator=(const CustomFontLoader&) = delete;

  /// Scan SD /fonts/ for valid families (directories with a readable font.json).
  /// Returns list of family base names.
  static std::vector<String> discoverFamilies();

  /// Load Regular for the active custom font from SPIFFS.
  /// Must be called after HalSpiffs::init() and before registerWithRenderer().
  /// `pt` is the desired point size (12/14/16/18); resolved to nearest available.
  bool init(const char* name, uint8_t pt);

  /// Lazily load one additional style variant. Heap-guarded.
  /// Returns false (silently) if heap < HEAP_GUARD_BYTES or style file absent.
  bool ensureStyle(EpdFontFamily::Style style);

  /// Register all currently-loaded styles with GfxRenderer.
  void registerWithRenderer(GfxRenderer& renderer);

  /// Copy full family from SD /fonts/<name>/ to SPIFFS /font/.
  /// font.json is written last as the atomic commit point.
  /// Returns false and leaves SPIFFS unchanged if any file copy fails.
  static bool copyFamilyToSpiffs(const char* name);

  /// Free all heap metrics blocks.
  void release();

  bool isLoaded() const { return _loaded; }
  int fontId() const { return _fontId; }

  /// Parse font.json from a SPIFFS path and populate sizes/fontId.
  static bool readFontJson(const char* jsonPath, std::vector<int>& outSizes, int& outFontId);

 private:
  // .epdfont file header (48 bytes)
  struct __attribute__((packed)) EpdFontHeader {
    char magic[4];    // "EPDF"
    uint8_t version;  // 1
    uint8_t flags;    // bit0=is2Bit, bit1=hasKerning, bit2=hasLigatures
    uint16_t glyphCount;
    uint32_t groupCount;
    uint32_t intervalCount;
    uint32_t kernLeftCount;
    uint32_t kernRightCount;
    uint32_t kernMatrixSize;
    uint32_t ligatureCount;
    uint8_t advanceY;
    uint8_t reserved0;
    int16_t ascender;
    int16_t descender;
    uint16_t kernLeftClassCount;
    uint16_t kernRightClassCount;
    uint32_t bitmapSize;
    uint16_t reserved1;
  };
  static_assert(sizeof(EpdFontHeader) == 48, "EpdFontHeader must be 48 bytes");

  struct StyleSlot {
    uint8_t* metricsBlock = nullptr;  // one malloc per loaded style
    EpdFontData data = {};
    SpiffsBitmapSource bitmapSrc;
    EpdFont font{&data};
    bool loaded = false;

    void release() {
      bitmapSrc.close();
      free(metricsBlock);
      metricsBlock = nullptr;
      data = {};
      loaded = false;
    }
  };

  StyleSlot _styles[4];  // 0=Regular 1=Bold 2=Italic 3=BoldItalic
  char _name[64] = {};
  uint8_t _pt = 0;
  int _fontId = 0;
  bool _loaded = false;

  static const char* styleChar(EpdFontFamily::Style style);
  bool loadStyle(EpdFontFamily::Style style);

  // Build an EpdFontFamily from currently-loaded slots.
  EpdFontFamily buildFamily() const;
};
