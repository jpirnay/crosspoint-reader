#pragma once

#include <InflateReader.h>

#include "EpdFontData.h"

class FontDecompressor {
 public:
  static constexpr uint16_t MAX_PAGE_GLYPHS = 512;
  static constexpr uint8_t MAX_PAGE_SLOTS = 4;  // One per font style (R/B/I/BI)

  FontDecompressor() = default;
  ~FontDecompressor();

  bool init();
  void deinit();

  // Returns pointer to decompressed bitmap data for the given glyph.
  // Checks the page buffer (from prewarm) first, then falls back to the hot group slot.
  const uint8_t* getBitmap(const EpdFontData* fontData, const EpdGlyph* glyph, uint32_t glyphIndex);

  // Free all cached data (page buffer + hot group).
  void clearCache();

  // Pre-scan UTF-8 text and extract needed glyph bitmaps into a flat page buffer.
  // Each group is decompressed once into a temp buffer; only needed glyphs are kept.
  // Returns the number of glyphs that couldn't be loaded (0 on full success).
  int prewarmCache(const EpdFontData* fontData, const char* utf8Text);

  struct Stats {
    uint32_t cacheHits = 0;
    uint32_t cacheMisses = 0;
    uint32_t decompressTimeMs = 0;
    uint16_t uniqueGroupsAccessed = 0;
    uint32_t pageBufferBytes = 0;  // pageBuffer allocation
    uint32_t pageGlyphsBytes = 0;  // pageGlyphs lookup table allocation
    uint32_t hotGroupBytes = 0;    // current hot group allocation
    uint32_t peakTempBytes = 0;    // largest temp buffer in prewarm
    uint32_t getBitmapTimeUs = 0;  // cumulative getBitmap time (micros)
    uint32_t getBitmapCalls = 0;   // number of getBitmap calls
  };
  void logStats(const char* label = "FDC");
  void resetStats();
  const Stats& getStats() const { return stats; }

 private:
  Stats stats;
  InflateReader inflateReader;

  // Page buffer slots: each style gets its own flat glyph buffer with sorted lookup.
  // Up to MAX_PAGE_SLOTS (4) styles can be prewarmed simultaneously.
  struct PageGlyphEntry {
    uint32_t glyphIndex;
    uint32_t bufferOffset;
    uint32_t alignedOffset;  // byte-aligned offset within its decompressed group (set during prewarm pre-scan)
  };
  struct PageSlot {
    uint8_t* buffer = nullptr;
    const EpdFontData* fontData = nullptr;
    PageGlyphEntry* glyphs = nullptr;
    uint16_t glyphCount = 0;
  };
  PageSlot pageSlots[MAX_PAGE_SLOTS] = {};
  uint8_t pageSlotCount = 0;

  // Measured maxima across all built-in fonts:
  //   uncompressedSize: 50 KB (notosans_18 / bookerly_18)
  //   compressedSize:   16 KB (bookerly_18)
  //   glyph dataLength: 500 B (bookerly_18)
  // Static BSS arrays eliminate per-page heap alloc/free and the fragmentation it causes.
  // Custom fonts must cap group sizes to these limits (enforced by fontconvert.py --binary).
  static constexpr uint32_t HOT_GROUP_BUF_SIZE = 51200;       // 50 KB uncompressed group
  static constexpr uint32_t COMPRESSED_STAGING_SIZE = 16384;  // 16 KB compressed staging (IBitmapSource path)
  static constexpr uint16_t HOT_GLYPH_BUF_SIZE = 512;         // largest packed single glyph

  // Hot group: last decompressed group (byte-aligned) for non-prewarmed fallback path.
  // Kept in byte-aligned format; individual glyphs are compacted on demand into _hotGlyphBuf.
  const EpdFontData* hotGroupFont = nullptr;
  uint16_t hotGroupIndex = UINT16_MAX;
  uint32_t _hotGroupBufUsed = 0;
  uint8_t _hotGroupBuf[HOT_GROUP_BUF_SIZE];

  // Staging buffer for reading compressed group bytes before inflation (IBitmapSource path).
  uint8_t _compressedStagingBuf[COMPRESSED_STAGING_SIZE];

  // Per-glyph LRU cache: caches packed glyph bitmaps decompressed from the hot group.
  // Avoids re-decompressing the same group when a glyph is requested repeatedly outside
  // the page buffer (e.g. space, punctuation, ligature glyphs across pages).
  // 16 entries × 512 B = 8 KB BSS. Open-addressing hash keyed by (fontData, glyphIndex).
  static constexpr uint8_t GLYPH_CACHE_SIZE = 16;

  struct GlyphCacheEntry {
    const EpdFontData* fontData = nullptr;
    uint32_t glyphIndex = UINT32_MAX;
    uint32_t lastUsed = 0;
    uint8_t bitmap[HOT_GLYPH_BUF_SIZE];
  };
  GlyphCacheEntry _glyphCache[GLYPH_CACHE_SIZE];
  uint32_t _glyphCacheCounter = 0;

  int glyphCacheLookup(const EpdFontData* fontData, uint32_t glyphIndex) const;
  int glyphCacheLruSlot() const;
  void glyphCacheStore(const EpdFontData* fontData, uint32_t glyphIndex, const uint8_t* packed);

  void freePageBuffer();
  void freeHotGroup();
  uint16_t getGroupIndex(const EpdFontData* fontData, uint32_t glyphIndex);
  uint32_t getAlignedOffset(const EpdFontData* fontData, uint16_t groupIndex, uint32_t glyphIndex);
  bool decompressGroup(const EpdFontData* fontData, uint16_t groupIndex, uint8_t* outBuf, uint32_t outSize);
  static void compactSingleGlyph(const uint8_t* alignedSrc, uint8_t* packedDst, uint8_t width, uint8_t height);
  static int32_t findGlyphIndex(const EpdFontData* fontData, uint32_t codepoint);
};
