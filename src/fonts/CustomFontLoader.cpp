#include "CustomFontLoader.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalSpiffs.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SPIFFS.h>

#include <cstring>

static constexpr size_t COPY_BUF_SIZE = 4096;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const char* CustomFontLoader::styleChar(EpdFontFamily::Style style) {
  switch (style) {
    case EpdFontFamily::BOLD:
      return "B";
    case EpdFontFamily::ITALIC:
      return "I";
    case EpdFontFamily::BOLD_ITALIC:
      return "BI";
    default:
      return "R";
  }
}

EpdFontFamily CustomFontLoader::buildFamily() const {
  const EpdFont* r = _styles[EpdFontFamily::REGULAR].loaded ? &_styles[EpdFontFamily::REGULAR].font : nullptr;
  const EpdFont* b = _styles[EpdFontFamily::BOLD].loaded ? &_styles[EpdFontFamily::BOLD].font : nullptr;
  const EpdFont* i = _styles[EpdFontFamily::ITALIC].loaded ? &_styles[EpdFontFamily::ITALIC].font : nullptr;
  const EpdFont* bi = _styles[EpdFontFamily::BOLD_ITALIC].loaded ? &_styles[EpdFontFamily::BOLD_ITALIC].font : nullptr;
  return EpdFontFamily(r, b, i, bi);
}

bool CustomFontLoader::readFontJson(const char* jsonPath, std::vector<int>& outSizes, int& outFontId) {
  File f = SPIFFS.open(jsonPath, FILE_READ);
  if (!f) return false;

  const size_t len = f.size();
  if (len == 0 || len > 512) {
    f.close();
    return false;
  }

  char buf[512];
  f.read(reinterpret_cast<uint8_t*>(buf), len);
  buf[len] = '\0';
  f.close();

  JsonDocument doc;
  if (deserializeJson(doc, buf) != DeserializationError::Ok) return false;

  outFontId = doc["fontId"] | 0;
  outSizes.clear();
  JsonArray sizes = doc["sizes"].as<JsonArray>();
  for (int s : sizes) outSizes.push_back(s);

  return outFontId != 0 && !outSizes.empty();
}

// ---------------------------------------------------------------------------
// discoverFamilies — scan SD /fonts/
// ---------------------------------------------------------------------------

std::vector<String> CustomFontLoader::discoverFamilies() {
  std::vector<String> result;
  if (!Storage.ready()) return result;

  HalFile dir = Storage.open(SD_FONTS_DIR);
  if (!dir || !dir.isDirectory()) return result;

  HalFile entry = dir.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      char nameBuf[64];
      entry.getName(nameBuf, sizeof(nameBuf));

      // Check for font.json inside
      String jsonPath = String(SD_FONTS_DIR) + "/" + nameBuf + "/font.json";
      if (Storage.exists(jsonPath.c_str())) {
        result.push_back(String(nameBuf));
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  return result;
}

// ---------------------------------------------------------------------------
// loadStyle — load one style variant from SPIFFS into heap
// ---------------------------------------------------------------------------

bool CustomFontLoader::loadStyle(EpdFontFamily::Style style) {
  const uint8_t si = static_cast<uint8_t>(style);
  if (_styles[si].loaded) return true;

  char path[128];
  snprintf(path, sizeof(path), "%s/%s_%u_%s.epdfont", SPIFFS_FONT_DIR, _name, _pt, styleChar(style));

  File f = SPIFFS.open(path, FILE_READ);
  if (!f) {
    // Style file absent — this is OK for non-Regular styles
    if (style != EpdFontFamily::REGULAR) {
      LOG_DBG("CFL", "Style %s not present, falling back to Regular", styleChar(style));
    } else {
      LOG_ERR("CFL", "Regular style not found: %s", path);
    }
    return false;
  }

  if (f.size() < 48) {
    f.close();
    LOG_ERR("CFL", "File too small: %s", path);
    return false;
  }

  EpdFontHeader hdr;
  if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
    f.close();
    LOG_ERR("CFL", "Header read failed: %s", path);
    return false;
  }
  f.close();

  if (memcmp(hdr.magic, "EPDF", 4) != 0 || hdr.version != 1) {
    LOG_ERR("CFL", "Bad magic/version: %s", path);
    return false;
  }

  // Compute metrics segment size
  // sizeof(EpdGlyph) = 16 (uint32_t dataOffset at offset 12 due to padding after uint16_t dataLength)
  // sizeof(EpdFontGroup) = 20 (uint32_t firstGlyphIndex at offset 16 due to padding after uint16_t glyphCount)
  const size_t glyphBytes = hdr.glyphCount * sizeof(EpdGlyph);
  const size_t intervalBytes = hdr.intervalCount * sizeof(EpdUnicodeInterval);
  const size_t groupBytes = hdr.groupCount * sizeof(EpdFontGroup);
  const size_t kernLBytes = hdr.kernLeftCount * sizeof(EpdKernClassEntry);
  const size_t kernRBytes = hdr.kernRightCount * sizeof(EpdKernClassEntry);
  const size_t kernMBytes = hdr.kernMatrixSize * sizeof(int8_t);
  const size_t ligBytes = hdr.ligatureCount * sizeof(EpdLigaturePair);
  const size_t metricsSize = glyphBytes + intervalBytes + groupBytes + kernLBytes + kernRBytes + kernMBytes + ligBytes;

  if (ESP.getFreeHeap() < HEAP_GUARD_BYTES + metricsSize) {
    LOG_ERR("CFL", "Heap guard: %u free, need %u+%u for style %s", ESP.getFreeHeap(), HEAP_GUARD_BYTES, metricsSize,
            styleChar(style));
    return false;
  }

  uint8_t* block = static_cast<uint8_t*>(malloc(metricsSize));
  if (!block) {
    LOG_ERR("CFL", "malloc(%u) failed for style %s", metricsSize, styleChar(style));
    return false;
  }

  // Read metrics segment (starts at byte 48)
  File f2 = SPIFFS.open(path, FILE_READ);
  if (!f2 || !f2.seek(48)) {
    free(block);
    if (f2) f2.close();
    LOG_ERR("CFL", "Cannot re-open/seek for metrics: %s", path);
    return false;
  }
  if (f2.read(block, metricsSize) != metricsSize) {
    free(block);
    f2.close();
    LOG_ERR("CFL", "Metrics read failed: %s", path);
    return false;
  }
  f2.close();

  // Wire EpdFontData pointers into the block
  StyleSlot& slot = _styles[si];
  uint8_t* p = block;

  slot.data.glyph = reinterpret_cast<const EpdGlyph*>(p);
  p += glyphBytes;
  slot.data.intervals = reinterpret_cast<const EpdUnicodeInterval*>(p);
  p += intervalBytes;
  slot.data.groups = reinterpret_cast<const EpdFontGroup*>(p);
  p += groupBytes;

  slot.data.intervalCount = hdr.intervalCount;
  slot.data.groupCount = static_cast<uint16_t>(hdr.groupCount);
  slot.data.glyphToGroup = nullptr;  // contiguous-group format
  slot.data.advanceY = hdr.advanceY;
  slot.data.ascender = hdr.ascender;
  slot.data.descender = hdr.descender;
  slot.data.is2Bit = (hdr.flags & 0x01) != 0;
  slot.data.bitmap = nullptr;  // not used — bitmapSource handles this

  if (hdr.flags & 0x02) {
    slot.data.kernLeftClasses = reinterpret_cast<const EpdKernClassEntry*>(p);
    p += kernLBytes;
    slot.data.kernRightClasses = reinterpret_cast<const EpdKernClassEntry*>(p);
    p += kernRBytes;
    slot.data.kernMatrix = reinterpret_cast<const int8_t*>(p);
    p += kernMBytes;
    slot.data.kernLeftEntryCount = static_cast<uint16_t>(hdr.kernLeftCount);
    slot.data.kernRightEntryCount = static_cast<uint16_t>(hdr.kernRightCount);
    slot.data.kernLeftClassCount = static_cast<uint8_t>(hdr.kernLeftClassCount);
    slot.data.kernRightClassCount = static_cast<uint8_t>(hdr.kernRightClassCount);
  } else {
    slot.data.kernLeftClasses = nullptr;
    slot.data.kernRightClasses = nullptr;
    slot.data.kernMatrix = nullptr;
    slot.data.kernLeftEntryCount = 0;
    slot.data.kernRightEntryCount = 0;
    slot.data.kernLeftClassCount = 0;
    slot.data.kernRightClassCount = 0;
  }

  if (hdr.flags & 0x04) {
    slot.data.ligaturePairs = reinterpret_cast<const EpdLigaturePair*>(p);
    slot.data.ligaturePairCount = hdr.ligatureCount;
  } else {
    slot.data.ligaturePairs = nullptr;
    slot.data.ligaturePairCount = 0;
  }

  // Open SPIFFS file handle for bitmap streaming
  const uint32_t bitmapOffset = static_cast<uint32_t>(48 + metricsSize);
  if (!slot.bitmapSrc.open(path, bitmapOffset)) {
    free(block);
    LOG_ERR("CFL", "Cannot open SpiffsBitmapSource: %s", path);
    return false;
  }

  slot.data.bitmapSource = &slot.bitmapSrc;
  slot.metricsBlock = block;
  // Re-point the EpdFont to the freshly-populated data
  slot.font = EpdFont(&slot.data);
  slot.loaded = true;

  LOG_DBG("CFL", "Loaded style %s from %s (%u B metrics)", styleChar(style), path, metricsSize);
  return true;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool CustomFontLoader::init(const char* name, uint8_t pt) {
  strncpy(_name, name, sizeof(_name) - 1);
  _name[sizeof(_name) - 1] = '\0';
  _pt = pt;

  // Read font.json for the fontId
  std::vector<int> sizes;
  if (!readFontJson(SPIFFS_FONT_JSON, sizes, _fontId)) {
    LOG_ERR("CFL", "Cannot read %s", SPIFFS_FONT_JSON);
    return false;
  }

  // Resolve requested pt to nearest available
  if (!sizes.empty()) {
    int best = sizes[0];
    for (int s : sizes) {
      if (abs(s - (int)pt) < abs(best - (int)pt)) best = s;
    }
    _pt = static_cast<uint8_t>(best);
  }

  if (!loadStyle(EpdFontFamily::REGULAR)) {
    return false;
  }

  _loaded = true;
  return true;
}

// ---------------------------------------------------------------------------
// ensureStyle
// ---------------------------------------------------------------------------

bool CustomFontLoader::ensureStyle(EpdFontFamily::Style style) {
  if (style == EpdFontFamily::REGULAR) return true;
  return loadStyle(style);
}

// ---------------------------------------------------------------------------
// registerWithRenderer
// ---------------------------------------------------------------------------

void CustomFontLoader::registerWithRenderer(GfxRenderer& renderer) { renderer.insertFont(_fontId, buildFamily()); }

// ---------------------------------------------------------------------------
// copyFamilyToSpiffs — atomic SD → SPIFFS copy, font.json last
// ---------------------------------------------------------------------------

bool CustomFontLoader::copyFamilyToSpiffs(const char* name) {
  // Remove old font slot first
  HalSpiffs::clearFontSlot();

  static const char* styles[] = {"R", "B", "I", "BI"};
  static const uint8_t sizes[] = {12, 14, 16, 18};

  // Ensure SPIFFS directory exists (SPIFFS has no real dirs, but paths need the prefix)
  // Just proceed — SPIFFS doesn't require mkdir.

  auto copyOneFile = [&](const char* sdPath, const char* spiffsPath) -> bool {
    HalFile src;
    if (!Storage.openFileForRead("CFL", sdPath, src)) return false;

    File dst = SPIFFS.open(spiffsPath, FILE_WRITE);
    if (!dst) {
      src.close();
      return false;
    }

    auto* buf = static_cast<uint8_t*>(malloc(COPY_BUF_SIZE));
    if (!buf) {
      dst.close();
      src.close();
      return false;
    }

    bool ok = true;
    while (src.available() > 0) {
      const int bytesRead = src.read(buf, COPY_BUF_SIZE);
      if (bytesRead <= 0) break;
      if (dst.write(buf, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
        ok = false;
        break;
      }
    }
    free(buf);
    dst.close();
    src.close();
    return ok;
  };

  // Copy all style files (missing styles are skipped — only Regular is required)
  bool hasRegular = false;
  for (uint8_t sz : sizes) {
    for (const char* st : styles) {
      char sdPath[128], spiffsPath[128];
      snprintf(sdPath, sizeof(sdPath), "%s/%s/%s_%u_%s.epdfont", SD_FONTS_DIR, name, name, sz, st);
      snprintf(spiffsPath, sizeof(spiffsPath), "%s/%s_%u_%s.epdfont", SPIFFS_FONT_DIR, name, sz, st);

      if (!Storage.exists(sdPath)) continue;

      if (!copyOneFile(sdPath, spiffsPath)) {
        LOG_ERR("CFL", "Failed to copy %s → %s", sdPath, spiffsPath);
        HalSpiffs::clearFontSlot();
        return false;
      }

      if (strcmp(st, "R") == 0) hasRegular = true;
    }
  }

  if (!hasRegular) {
    LOG_ERR("CFL", "No Regular style found for '%s'", name);
    HalSpiffs::clearFontSlot();
    return false;
  }

  // Copy font.json last — this is the atomic commit point
  char sdJsonPath[128];
  snprintf(sdJsonPath, sizeof(sdJsonPath), "%s/%s/font.json", SD_FONTS_DIR, name);
  if (!copyOneFile(sdJsonPath, SPIFFS_FONT_JSON)) {
    LOG_ERR("CFL", "Failed to copy font.json for '%s'", name);
    HalSpiffs::clearFontSlot();
    return false;
  }

  LOG_DBG("CFL", "Copied font '%s' to SPIFFS", name);
  return true;
}

// ---------------------------------------------------------------------------
// release
// ---------------------------------------------------------------------------

void CustomFontLoader::release() {
  for (auto& slot : _styles) slot.release();
  _loaded = false;
  _fontId = 0;
  _pt = 0;
  _name[0] = '\0';
}
