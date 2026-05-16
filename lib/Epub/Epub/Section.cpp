#include "Section.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <algorithm>

#include "Epub/css/CssParser.h"
#include "EpubIndexingPolicy.h"
#include "Page.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

Section::Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer)
    : epub(epub), spineIndex(spineIndex), renderer(renderer) {}

// Constructor and destructor are defined here (not in the header) so that
// std::unique_ptr<ChapterHtmlSlimParser> can be deleted with the full type visible —
// ChapterHtmlSlimParser is forward-declared in Section.h but fully defined in this TU.
Section::~Section() = default;

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 28;

namespace header {
constexpr uint32_t kVersion = 0;
constexpr uint32_t kFontId = kVersion + sizeof(uint8_t);
constexpr uint32_t kLineCompression = kFontId + sizeof(int);
constexpr uint32_t kExtraParagraphSpacing = kLineCompression + sizeof(float);
constexpr uint32_t kParagraphAlignment = kExtraParagraphSpacing + sizeof(bool);
constexpr uint32_t kViewportWidth = kParagraphAlignment + sizeof(uint8_t);
constexpr uint32_t kViewportHeight = kViewportWidth + sizeof(uint16_t);
constexpr uint32_t kHyphenationEnabled = kViewportHeight + sizeof(uint16_t);
constexpr uint32_t kEmbeddedStyle = kHyphenationEnabled + sizeof(bool);
constexpr uint32_t kBionicReadingEnabled = kEmbeddedStyle + sizeof(bool);
constexpr uint32_t kImageRendering = kBionicReadingEnabled + sizeof(bool);
constexpr uint32_t kParseComplete = kImageRendering + sizeof(uint8_t);
constexpr uint32_t kPageCount = kParseComplete + sizeof(bool);
constexpr uint32_t kPageLut = kPageCount + sizeof(uint16_t);
constexpr uint32_t kAnchorMap = kPageLut + sizeof(uint32_t);
constexpr uint32_t kParagraphLut = kAnchorMap + sizeof(uint32_t);
constexpr uint32_t kSize = kParagraphLut + sizeof(uint32_t);
}  // namespace header

// On-disk paragraph LUT entry: u32 xhtmlByteOffset + u16 paragraphIndex + u16 listItemIndex.
// listItemIndex is the running <li> count at page-break time; together with
// paragraphIndex it lets KOReader-supplied <p>- and <li>-anchored XPaths snap to
// the exact page on download.
constexpr uint32_t PARAGRAPH_LUT_ENTRY_SIZE = sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t);
inline uint32_t paragraphLutEntryOffset(uint32_t lutStart, uint16_t page) {
  return lutStart + page * PARAGRAPH_LUT_ENTRY_SIZE;
}
}  // namespace

#include <algorithm>

namespace {
constexpr uint32_t FNV_PRIME = 0x01000193;         // 16777619
constexpr uint32_t FNV_OFFSET_BASIS = 0x811C9DC5;  // 2166136261

// On constrained targets, loading the CSS rules map before chapter parsing can
// consume a large share of available heap and increase parse truncation risk.
// Allow compile-time override for tuning.
#ifndef SCT_EMBEDDED_STYLE_MIN_FREE_HEAP_BYTES
#define SCT_EMBEDDED_STYLE_MIN_FREE_HEAP_BYTES (96 * 1024)
#endif

#ifndef SCT_EMBEDDED_STYLE_MIN_CONTIG_HEAP_BYTES
#define SCT_EMBEDDED_STYLE_MIN_CONTIG_HEAP_BYTES (56 * 1024)
#endif

constexpr uint32_t EMBEDDED_STYLE_MIN_FREE_HEAP_BYTES = SCT_EMBEDDED_STYLE_MIN_FREE_HEAP_BYTES;
constexpr uint32_t EMBEDDED_STYLE_MIN_CONTIG_HEAP_BYTES = SCT_EMBEDDED_STYLE_MIN_CONTIG_HEAP_BYTES;

uint32_t fnv1a(const uint8_t* data, size_t length) {
  uint32_t hash = FNV_OFFSET_BASIS;
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= FNV_PRIME;
  }
  return hash;
}
}  // namespace

uint32_t Section::calculatePropertyHash(int fontId, float lineCompression, bool extraParagraphSpacing,
                                        uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                                        bool hyphenationEnabled, bool embeddedStyle, bool bionicReadingEnabled,
                                        uint8_t imageRendering) {
  uint8_t buffer[64];
  size_t offset = 0;

  auto append = [&](const void* ptr, size_t size) {
    memcpy(buffer + offset, ptr, size);
    offset += size;
  };

  append(&fontId, sizeof(fontId));
  append(&lineCompression, sizeof(lineCompression));
  append(&extraParagraphSpacing, sizeof(extraParagraphSpacing));
  append(&paragraphAlignment, sizeof(paragraphAlignment));
  append(&viewportWidth, sizeof(viewportWidth));
  append(&viewportHeight, sizeof(viewportHeight));
  append(&hyphenationEnabled, sizeof(hyphenationEnabled));
  append(&embeddedStyle, sizeof(embeddedStyle));
  append(&bionicReadingEnabled, sizeof(bionicReadingEnabled));
  append(&imageRendering, sizeof(imageRendering));

  return fnv1a(buffer, offset);
}

std::string Section::getSectionFilePath(uint32_t propertyHash) const {
  char buf[32];
  snprintf(buf, sizeof(buf), "%d_%08x", spineIndex, propertyHash);
  return epub->getCachePath() + "/sections/" + buf + ".bin";
}

// Incremental cache paths — share the stem with the legacy .bin but use separate extensions.
static std::string getSecPath(const std::string& stem) { return stem + ".sec"; }
static std::string getLutPath(const std::string& stem) { return stem + ".lut"; }
static std::string getAncPath(const std::string& stem) { return stem + ".anc"; }

std::string Section::getSectionStem(uint32_t propertyHash) const {
  char buf[32];
  snprintf(buf, sizeof(buf), "%d_%08x", spineIndex, propertyHash);
  return epub->getCachePath() + "/sections/" + buf;
}

std::string Section::getImageBasePath(uint32_t propertyHash) const {
  char buf[32];
  snprintf(buf, sizeof(buf), "img_%d_%08x_", spineIndex, propertyHash);
  return epub->getCachePath() + "/" + buf;
}

struct SectionVariant {
  std::string filename;
  uint16_t date;
  uint16_t time;
};

void Section::evictOldVariants() const {
  // We keep up to 5 most recently accessed/modified variants to prevent SD card bloat
  constexpr size_t MAX_VARIANTS = 5;

  std::string sectionsDir = epub->getCachePath() + "/sections";
  auto files = Storage.listFiles(sectionsDir.c_str(), 100);

  std::vector<SectionVariant> variants;

  // Find all cache variants belonging to this spineIndex — both .bin and .sec formats
  char prefix[16];
  snprintf(prefix, sizeof(prefix), "%d_", spineIndex);

  for (const auto& file : files) {
    if (!file.startsWith(prefix)) continue;
    const bool isBin = file.endsWith(".bin");
    const bool isSec = file.endsWith(".sec");
    if (!isBin && !isSec) continue;

    HalFile hf = Storage.open((sectionsDir + "/" + file.c_str()).c_str(), O_RDONLY);
    uint16_t md = 0, mt = 0;
    if (hf) hf.getModifyDateTime(&md, &mt);
    variants.push_back({file.c_str(), md, mt});
  }

  if (variants.size() <= MAX_VARIANTS) return;

  // Sort descending by modified date and time
  std::sort(variants.begin(), variants.end(), [](const SectionVariant& a, const SectionVariant& b) {
    if (a.date != b.date) return a.date > b.date;
    return a.time > b.time;
  });

  // Delete everything after MAX_VARIANTS limit
  for (size_t i = MAX_VARIANTS; i < variants.size(); ++i) {
    const std::string& fname = variants[i].filename;
    size_t dot = fname.rfind('.');
    size_t underscore = fname.find('_');

    const bool isBinVariant = (dot != std::string::npos && fname.substr(dot) == ".bin");

    if (isBinVariant) {
      Storage.remove((sectionsDir + "/" + fname).c_str());
      LOG_DBG("SCT", "Evicted old section cache: %s", fname.c_str());
    } else {
      // .sec variant — remove the whole stem set (.sec, .lut, .anc, .raw)
      std::string stem = sectionsDir + "/" + (dot != std::string::npos ? fname.substr(0, dot) : fname);
      for (const char* ext : {".sec", ".lut", ".anc", ".raw"}) {
        std::string p = stem + ext;
        if (Storage.exists(p.c_str())) {
          Storage.remove(p.c_str());
          LOG_DBG("SCT", "Evicted old section cache: %s%s",
                  (dot != std::string::npos ? fname.substr(0, dot) : fname).c_str(), ext);
        }
      }
    }

    // Extract the hash to also clean up associated images
    // Filename format: spineIndex_hash.{bin,sec,...}
    if (underscore != std::string::npos && dot != std::string::npos && dot > underscore) {
      std::string hashStr = fname.substr(underscore + 1, dot - underscore - 1);
      uint32_t parsedHash = strtoul(hashStr.c_str(), nullptr, 16);
      if (parsedHash != 0 || hashStr == "00000000") {
        std::string imgBasePath = getImageBasePath(parsedHash);
        auto rootFiles = Storage.listFiles(epub->getCachePath().c_str(), 100);
        size_t lastSlash = imgBasePath.find_last_of('/');
        std::string imgPrefix = (lastSlash != std::string::npos) ? imgBasePath.substr(lastSlash + 1) : imgBasePath;

        for (const auto& rf : rootFiles) {
          if (rf.startsWith(imgPrefix.c_str())) {
            Storage.remove((epub->getCachePath() + "/" + rf.c_str()).c_str());
            LOG_DBG("SCT", "Evicted old image cache: %s", rf.c_str());
          }
        }
      }
    }
  }
}

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    LOG_ERR("SCT", "File not open for writing page %d", pageCount);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("SCT", "Failed to serialize page %d", pageCount);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", pageCount);

  pageCount++;
  return position;
}

void Section::writeSectionFileHeader(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                     const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                     const uint16_t viewportHeight, const bool hyphenationEnabled,
                                     const bool embeddedStyle, const bool bionicReadingEnabled,
                                     const uint8_t imageRendering) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return;
  }
  static_assert(header::kSize == sizeof(SECTION_FILE_VERSION) + sizeof(fontId) + sizeof(lineCompression) +
                                     sizeof(extraParagraphSpacing) + sizeof(paragraphAlignment) +
                                     sizeof(viewportWidth) + sizeof(viewportHeight) + sizeof(hyphenationEnabled) +
                                     sizeof(embeddedStyle) + sizeof(bionicReadingEnabled) + sizeof(imageRendering) +
                                     sizeof(bool) + sizeof(pageCount) + sizeof(uint32_t) + sizeof(uint32_t) +
                                     sizeof(uint32_t),
                "Header size mismatch");
  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, extraParagraphSpacing);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, embeddedStyle);
  serialization::writePod(file, bionicReadingEnabled);
  serialization::writePod(file, imageRendering);
  serialization::writePod(file, false);      // Placeholder for parseComplete (patched later)
  serialization::writePod(file, pageCount);  // Placeholder for page count (will be initially 0, patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for anchor map offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for paragraph LUT offset (patched later)
}

bool Section::loadSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                              const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                              const uint16_t viewportHeight, const bool hyphenationEnabled, const bool embeddedStyle,
                              const bool bionicReadingEnabled, const uint8_t imageRendering) {
  truncatedCache = false;
  uint32_t propertyHash =
      calculatePropertyHash(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                            viewportHeight, hyphenationEnabled, embeddedStyle, bionicReadingEnabled, imageRendering);

  // --- Incremental cache format: try .sec/.lut first ---
  {
    const std::string stem = getSectionStem(propertyHash);
    const std::string secPath = getSecPath(stem);
    const std::string lutPath = getLutPath(stem);

    FsFile secFile;
    FsFile lutFile;
    if (Storage.openFileForRead("SCT", secPath, secFile) && Storage.openFileForRead("SCT", lutPath, lutFile)) {
      // Validate .sec header
      uint8_t version;
      serialization::readPod(secFile, version);
      bool headerOk = (version == SECTION_FILE_VERSION);

      if (headerOk) {
        int fileFontId;
        float fileLineCompression;
        bool fileExtraParagraphSpacing;
        uint8_t fileParagraphAlignment;
        uint16_t fileViewportWidth, fileViewportHeight;
        bool fileHyphenationEnabled, fileEmbeddedStyle, fileBionicReadingEnabled;
        uint8_t fileImageRendering;
        serialization::readPod(secFile, fileFontId);
        serialization::readPod(secFile, fileLineCompression);
        serialization::readPod(secFile, fileExtraParagraphSpacing);
        serialization::readPod(secFile, fileParagraphAlignment);
        serialization::readPod(secFile, fileViewportWidth);
        serialization::readPod(secFile, fileViewportHeight);
        serialization::readPod(secFile, fileHyphenationEnabled);
        serialization::readPod(secFile, fileEmbeddedStyle);
        serialization::readPod(secFile, fileBionicReadingEnabled);
        serialization::readPod(secFile, fileImageRendering);

        if (fontId != fileFontId || lineCompression != fileLineCompression ||
            extraParagraphSpacing != fileExtraParagraphSpacing || paragraphAlignment != fileParagraphAlignment ||
            viewportWidth != fileViewportWidth || viewportHeight != fileViewportHeight ||
            hyphenationEnabled != fileHyphenationEnabled || embeddedStyle != fileEmbeddedStyle ||
            bionicReadingEnabled != fileBionicReadingEnabled || imageRendering != fileImageRendering) {
          headerOk = false;
        }
      }

      if (headerOk) {
        // Load LUT entries — each is a uint32_t page-data offset in .sec
        const uint32_t lutFileSize = lutFile.size();
        const uint32_t entryCount = lutFileSize / sizeof(uint32_t);
        if (entryCount > 0 && entryCount <= 10000) {
          lut.resize(entryCount);
          for (uint32_t& pos : lut) {
            serialization::readPod(lutFile, pos);
          }
          pageCount = static_cast<uint16_t>(entryCount);
          secFile.close();
          lutFile.close();

          // Open .sec for reading (page loads seek into it)
          _secPath = secPath;
          _lutPath = getLutPath(stem);
          if (!Storage.openFileForRead("SCT", _secPath, file)) {
            lut.clear();
            pageCount = 0;
          } else {
            // Check for .anc to build TOC boundaries; if absent, build is still in progress
            const std::string ancPath = getAncPath(stem);
            if (Storage.exists(ancPath.c_str())) {
              _ancPath = ancPath;
              FsFile ancFile;
              if (Storage.openFileForRead("SCT", _ancPath, ancFile)) {
                // .anc format: uint16_t anchorCount, then [string key + uint16_t page] entries.
                // Load anchors into memory and resolve TOC boundaries.
                uint16_t anchorCount = 0;
                serialization::readPod(ancFile, anchorCount);
                std::vector<std::pair<std::string, uint16_t>> anchors;
                anchors.reserve(anchorCount);
                for (uint16_t ai = 0; ai < anchorCount; ai++) {
                  std::string key;
                  uint16_t pg;
                  serialization::readString(ancFile, key);
                  serialization::readPod(ancFile, pg);
                  anchors.emplace_back(std::move(key), pg);
                }
                ancFile.close();
                buildTocBoundaries(anchors);
              }
              _buildState = BuildState::Complete;
            } else {
              _buildState = BuildState::InProgress;
            }
            LOG_DBG("SCT", "Loaded incremental cache: spine=%d pages=%u state=%s", spineIndex, pageCount,
                    _buildState == BuildState::Complete ? "complete" : "partial");
            return true;
          }
        }
      }

      secFile.close();
      lutFile.close();
    }
  }
  // --- End incremental cache ---

  filePath = getSectionFilePath(propertyHash);

  bool usingEmbeddedStyleFallback = false;
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    // Fallback: allow loading a no-CSS cache variant when embedded CSS is enabled.
    if (embeddedStyle) {
      const uint32_t fallbackHash =
          calculatePropertyHash(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                                viewportHeight, hyphenationEnabled, false, bionicReadingEnabled, imageRendering);
      const std::string fallbackPath = getSectionFilePath(fallbackHash);
      if (Storage.openFileForRead("SCT", fallbackPath, file)) {
        filePath = fallbackPath;
        usingEmbeddedStyleFallback = true;
        LOG_INF("SCT", "Using no-CSS section cache fallback: %s", filePath.c_str());
      } else {
        return false;
      }
    } else {
      return false;
    }
  }

  // Match parameters
  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION) {
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();  // closes file before removal
      return false;
    }

    int fileFontId;
    uint16_t fileViewportWidth, fileViewportHeight;
    float fileLineCompression;
    bool fileExtraParagraphSpacing;
    uint8_t fileParagraphAlignment;
    bool fileHyphenationEnabled;
    bool fileEmbeddedStyle;
    bool fileBionicReadingEnabled;
    uint8_t fileImageRendering;
    bool fileParseComplete;
    serialization::readPod(file, fileFontId);
    serialization::readPod(file, fileLineCompression);
    serialization::readPod(file, fileExtraParagraphSpacing);
    serialization::readPod(file, fileParagraphAlignment);
    serialization::readPod(file, fileViewportWidth);
    serialization::readPod(file, fileViewportHeight);
    serialization::readPod(file, fileHyphenationEnabled);
    serialization::readPod(file, fileEmbeddedStyle);
    serialization::readPod(file, fileBionicReadingEnabled);
    serialization::readPod(file, fileImageRendering);
    serialization::readPod(file, fileParseComplete);

    const bool embeddedStyleMatches =
        (embeddedStyle == fileEmbeddedStyle) || (usingEmbeddedStyleFallback && !fileEmbeddedStyle);
    if (fontId != fileFontId || lineCompression != fileLineCompression ||
        extraParagraphSpacing != fileExtraParagraphSpacing || paragraphAlignment != fileParagraphAlignment ||
        viewportWidth != fileViewportWidth || viewportHeight != fileViewportHeight ||
        hyphenationEnabled != fileHyphenationEnabled || !embeddedStyleMatches ||
        bionicReadingEnabled != fileBionicReadingEnabled || imageRendering != fileImageRendering) {
      LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
      clearCache();  // closes file before removal
      return false;
    }

    truncatedCache = !fileParseComplete;
  }

  serialization::readPod(file, pageCount);

  // Sanity check: same upper bound used by TextBlock::deserialize for word count
  if (pageCount > 10000) {
    LOG_ERR("SCT", "Deserialization failed: page count %u exceeds maximum", pageCount);
    clearCache();
    return false;
  }

  // Load LUT into memory (file is now positioned at the lutOffset field)
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);
  lut.resize(pageCount);
  if (!file.seek(lutOffset)) {
    LOG_ERR("SCT", "Deserialization failed: seek to LUT offset %u failed", lutOffset);
    clearCache();
    return false;
  }
  for (uint32_t& pos : lut) {
    serialization::readPod(file, pos);
    if (pos < header::kSize || pos >= lutOffset) {
      LOG_ERR("SCT", "Deserialization failed: LUT entry %u out of range [%u, %u)", pos, header::kSize, lutOffset);
      clearCache();
      return false;
    }
  }
  // Build TOC boundaries by scanning anchor data from the still-open file,
  // matching only the TOC anchors we need (avoids loading all anchors into memory).
  buildTocBoundariesFromFile(file);

  // File is intentionally left open; subsequent loadPageFromSectionFile() calls
  // seek within this handle instead of re-opening the file each time.
  LOG_DBG("SCT", "Deserialization succeeded: %d pages, LUT cached", pageCount);
  return true;
}

bool Section::clearCache() {
  file.close();  // Must be closed before removal on FAT32
  _lutFile.close();
  _rawFile.close();
  _parser.reset();
  _buildState = BuildState::Idle;
  _rawRemaining = 0;
  lut.clear();
  tocBoundaries.clear();
  pageCount = 0;
  currentPage = 0;
  truncatedCache = false;

  bool anyRemoved = false;

  // Remove incremental cache files if present
  for (const std::string* path : {&_secPath, &_lutPath, &_ancPath, &_rawPath}) {
    if (!path->empty() && Storage.exists(path->c_str())) {
      Storage.remove(path->c_str());
      anyRemoved = true;
    }
  }
  _secPath.clear();
  _lutPath.clear();
  _ancPath.clear();
  _rawPath.clear();

  if (!Storage.exists(filePath.c_str())) {
    if (!anyRemoved) {
      LOG_DBG("SCT", "Cache does not exist, no action needed");
    }
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("SCT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("SCT", "Cache cleared successfully");
  return true;
}

bool Section::createSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                const uint16_t viewportHeight, const bool hyphenationEnabled, const bool embeddedStyle,
                                const bool bionicReadingEnabled, const uint8_t imageRendering,
                                const std::function<void(int)>& progressFn, const bool skipEviction) {
  if (!skipEviction) {
    evictOldVariants();
  }
  if (embeddedStyle) {
    const uint32_t freeHeap = esp_get_free_heap_size();
    const uint32_t contigHeap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT);
    if (freeHeap < EMBEDDED_STYLE_MIN_FREE_HEAP_BYTES || contigHeap < EMBEDDED_STYLE_MIN_CONTIG_HEAP_BYTES) {
      LOG_INF("SCT",
              "Low heap for embedded CSS (free=%lu contig=%lu, need free>=%lu contig>=%lu); "
              "building no-CSS section cache",
              freeHeap, contigHeap, static_cast<uint32_t>(EMBEDDED_STYLE_MIN_FREE_HEAP_BYTES),
              static_cast<uint32_t>(EMBEDDED_STYLE_MIN_CONTIG_HEAP_BYTES));
      return createSectionFile(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                               viewportHeight, hyphenationEnabled, false, bionicReadingEnabled, imageRendering,
                               progressFn, true);
    }
  }

  uint32_t propertyHash =
      calculatePropertyHash(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                            viewportHeight, hyphenationEnabled, embeddedStyle, bionicReadingEnabled, imageRendering);
  filePath = getSectionFilePath(propertyHash);

  const uint32_t phaseTotalStart = millis();
  const auto localPath = epub->getSpineItem(spineIndex).href;

  // Create cache directory if it doesn't exist
  {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  // Get inflated size up-front so the parser can choose progress granularity.
  const uint32_t phaseSetupStart = millis();
  size_t inflatedSize = 0;
  if (!epub->getItemSize(localPath, &inflatedSize)) {
    LOG_ERR("SCT", "Failed to get inflated size for %s", localPath.c_str());
    return false;
  }

  if (!Storage.openFileForWrite("SCT", filePath, file)) {
    return false;
  }
  writeSectionFileHeader(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled, embeddedStyle, bionicReadingEnabled, imageRendering);
  std::vector<uint32_t> lut = {};

  // Derive the content base directory and image cache path prefix for the parser
  size_t lastSlash = localPath.find_last_of('/');
  std::string contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  std::string imageBasePath = getImageBasePath(propertyHash);

  CssParser* cssParser = nullptr;
  if (embeddedStyle) {
    cssParser = epub->getCssParser();
    if (cssParser) {
      if (!cssParser->loadFromCache()) {
        LOG_ERR("SCT", "Failed to load CSS from cache");
      }
      cssParser->resetResolveStats();
    }
  }

  // Collect TOC anchors for this spine so the parser can insert page breaks at chapter boundaries
  std::vector<std::string> tocAnchors;
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex >= 0) {
    for (int i = startTocIndex; i < epub->getTocItemsCount(); i++) {
      auto entry = epub->getTocItem(i);
      if (entry.spineIndex != spineIndex) break;
      if (!entry.anchor.empty()) {
        tocAnchors.push_back(std::move(entry.anchor));
      }
    }
  }

  ChapterHtmlSlimParser visitor(
      epub, renderer, fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth, viewportHeight,
      hyphenationEnabled, bionicReadingEnabled,
      [this, &lut](std::unique_ptr<Page> page) { lut.emplace_back(this->onPageComplete(std::move(page))); },
      embeddedStyle, contentBase, imageBasePath, imageRendering, std::move(tocAnchors), progressFn, cssParser);
  Hyphenator::setPreferredLanguage(epub->getLanguage());

  if (!visitor.setup(inflatedSize)) {
    LOG_ERR("SCT", "Failed to set up chapter parser");
    file.close();
    Storage.remove(filePath.c_str());
    if (cssParser) {
      cssParser->clear();
    }
    return false;
  }
  const uint32_t setupMs = millis() - phaseSetupStart;

  // Stream EPUB item content directly into the parser — no temp file, no second SD pass.
  const uint32_t phaseParseStart = millis();
  const bool streamOk = epub->readItemContentsToStream(localPath, visitor, 1024);
  const bool finalizeOk = visitor.finalize();
  const bool parserStreamOk = visitor.streamSucceeded();
  if (cssParser) {
    cssParser->logResolveStats(localPath.c_str());
  }
  const bool parseComplete = streamOk && finalizeOk && parserStreamOk;
  bool success = parseComplete;
  const bool hasParsedPages = pageCount > 0;
  const uint32_t parseMs = millis() - phaseParseStart;
  // streamMs is no longer a separate phase (SD-write of temp file is gone); keep the
  // log breakdown stable by reporting it as 0.
  constexpr uint32_t streamMs = 0;

  const uint32_t phaseFinalizeStart = millis();
  if (!success) {
    // If parsing fails mid-stream due low memory but some pages were already serialized,
    // keep the partial section cache so the chapter remains readable instead of failing hard.
    if (hasParsedPages) {
      LOG_ERR("SCT", "Parse incomplete; keeping partial section cache with %u pages (stream=%d finalize=%d parser=%d)",
              pageCount, streamOk ? 1 : 0, finalizeOk ? 1 : 0, parserStreamOk ? 1 : 0);
      success = true;
    } else if (embeddedStyle) {
      LOG_ERR("SCT",
              "Parse failed with embedded CSS enabled; retrying section creation with embeddedStyle=0 "
              "(stream=%d finalize=%d parser=%d)",
              streamOk ? 1 : 0, finalizeOk ? 1 : 0, parserStreamOk ? 1 : 0);
      file.close();
      Storage.remove(filePath.c_str());
      if (cssParser) {
        cssParser->clear();
      }
      return createSectionFile(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                               viewportHeight, hyphenationEnabled, false, bionicReadingEnabled, imageRendering,
                               progressFn, true);
    } else {
      LOG_ERR("SCT", "Failed to parse XML and build pages (stream=%d finalize=%d parser=%d)", streamOk ? 1 : 0,
              finalizeOk ? 1 : 0, parserStreamOk ? 1 : 0);
      file.close();
      Storage.remove(filePath.c_str());
      if (cssParser) {
        cssParser->clear();
      }
      return false;
    }
  }
  const uint32_t fileSize = static_cast<uint32_t>(inflatedSize);

  const uint32_t lutOffset = file.position();
  bool hasFailedLutRecords = false;
  // Write LUT
  for (const uint32_t& pos : lut) {
    if (pos == 0) {
      hasFailedLutRecords = true;
      break;
    }
    serialization::writePod(file, pos);
  }

  if (hasFailedLutRecords) {
    LOG_ERR("SCT", "Failed to write LUT due to invalid page positions");
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  // Write anchor-to-page map for fragment navigation (TOC + footnote targets)
  const uint32_t anchorMapOffset = file.position();
  const auto& anchors = visitor.getAnchors();
  serialization::writePod(file, static_cast<uint16_t>(anchors.size()));
  for (const auto& [anchor, page] : anchors) {
    serialization::writeString(file, anchor);
    serialization::writePod(file, page);
  }

  // Write per-page paragraph LUT: count + array of {xhtmlByteOffset(u32), paragraphIndex(u16)}.
  // The byte offset lets findXPathForParagraph seek near the target paragraph without scanning
  // from the beginning of the XHTML file, reducing SD reads on large chapters.
  const uint32_t paragraphLutOffset = file.position();
  const auto& paragraphLut = visitor.getParagraphLutPerPage();
  if (paragraphLut.size() != static_cast<size_t>(pageCount)) {
    LOG_ERR("SCT", "Paragraph LUT size mismatch: lut=%u pageCount=%u", static_cast<uint32_t>(paragraphLut.size()),
            static_cast<uint32_t>(pageCount));
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }
  serialization::writePod(file, static_cast<uint16_t>(paragraphLut.size()));
  for (const auto& entry : paragraphLut) {
    serialization::writePod(file, entry.xhtmlByteOffset);
    serialization::writePod(file, entry.paragraphIndex);
    serialization::writePod(file, entry.listItemIndex);
  }

  // Patch header with final parseComplete/pageCount and offsets.
  const size_t headerPatchStart = header::kParseComplete;
  if (!file.seek(headerPatchStart)) {
    LOG_ERR("SCT", "Failed to seek to section header patch offset %u", header::kParseComplete);
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }
  serialization::writePod(file, parseComplete);
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  serialization::writePod(file, anchorMapOffset);
  serialization::writePod(file, paragraphLutOffset);
  file.flush();

  const size_t expectedHeaderPatchEnd = headerPatchStart + sizeof(parseComplete) + sizeof(pageCount) +
                                        sizeof(lutOffset) + sizeof(anchorMapOffset) + sizeof(paragraphLutOffset);
  if (file.position() != expectedHeaderPatchEnd) {
    LOG_ERR("SCT", "Section header patch write failed: wrote %u bytes at offset %u",
            static_cast<unsigned>(file.position() - headerPatchStart), static_cast<unsigned>(headerPatchStart));
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  if (cssParser) {
    cssParser->clear();
  }

  buildTocBoundaries(anchors);

  file.close();

  // Cache the LUT in memory and open the file for reading so that
  // subsequent loadPageFromSectionFile() calls can seek directly without re-opening.
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    LOG_ERR("SCT", "Failed to open section file for reading after creation");
    return false;
  }
  truncatedCache = !parseComplete;
  this->lut = std::move(lut);
  const uint32_t finalizeMs = millis() - phaseFinalizeStart;
  const uint32_t totalMs = millis() - phaseTotalStart;
  LOG_DBG("SCT", "createSectionFile spine=%d total=%ums (stream=%u setup=%u parse=%u finalize=%u) pages=%u bytes=%u",
          spineIndex, totalMs, streamMs, setupMs, parseMs, finalizeMs, pageCount, fileSize);
  return true;
}

bool Section::beginIncrementalBuild(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                    const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                    const uint16_t viewportHeight, const bool hyphenationEnabled,
                                    const bool embeddedStyle, const bool bionicReadingEnabled,
                                    const uint8_t imageRendering) {
  if (_buildState == BuildState::InProgress) {
    return true;  // Already building
  }

  evictOldVariants();

  const uint32_t propertyHash =
      calculatePropertyHash(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                            viewportHeight, hyphenationEnabled, embeddedStyle, bionicReadingEnabled, imageRendering);
  const std::string stem = getSectionStem(propertyHash);
  _secPath = getSecPath(stem);
  _lutPath = getLutPath(stem);
  _ancPath = getAncPath(stem);
  _rawPath = stem + ".raw";
  _imagePath = getImageBasePath(propertyHash);

  // Ensure sections directory exists
  {
    const std::string sectionsDir = epub->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  // Handle embedded-style heap check (same as createSectionFile)
  bool useEmbeddedStyle = embeddedStyle;
  if (useEmbeddedStyle) {
    const uint32_t freeHeap = esp_get_free_heap_size();
    const uint32_t contigHeap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT);
    if (freeHeap < EMBEDDED_STYLE_MIN_FREE_HEAP_BYTES || contigHeap < EMBEDDED_STYLE_MIN_CONTIG_HEAP_BYTES) {
      LOG_INF("SCT", "Low heap for embedded CSS, incremental build without CSS");
      useEmbeddedStyle = false;
    }
  }

  // Extract EPUB item to a temp .raw file so we can feed it in chunks across multiple pump() calls.
  // This mirrors createSectionFile but is necessary because readItemContentsToStream has no
  // mid-stream resume mechanism — it loops internally and cannot be paused.
  const auto localPath = epub->getSpineItem(spineIndex).href;
  size_t inflatedSize = 0;
  if (!epub->getItemSize(localPath, &inflatedSize)) {
    LOG_ERR("SCT", "beginIncrementalBuild: failed to get inflated size for %s", localPath.c_str());
    _buildState = BuildState::Failed;
    return false;
  }
  {
    FsFile rawWriteFile;
    if (!Storage.openFileForWrite("SCT", _rawPath, rawWriteFile)) {
      LOG_ERR("SCT", "beginIncrementalBuild: failed to open .raw for writing");
      _buildState = BuildState::Failed;
      return false;
    }
    const bool extractOk = epub->readItemContentsToStream(localPath, rawWriteFile, 1024);
    rawWriteFile.close();
    if (!extractOk) {
      LOG_ERR("SCT", "beginIncrementalBuild: failed to extract EPUB item to .raw");
      Storage.remove(_rawPath.c_str());
      _buildState = BuildState::Failed;
      return false;
    }
  }
  if (!Storage.openFileForRead("SCT", _rawPath, _rawFile)) {
    LOG_ERR("SCT", "beginIncrementalBuild: failed to open .raw for reading");
    _buildState = BuildState::Failed;
    return false;
  }
  _rawRemaining = inflatedSize;

  // Open .sec for writing (page data)
  if (!Storage.openFileForWrite("SCT", _secPath, file)) {
    LOG_ERR("SCT", "beginIncrementalBuild: failed to open .sec for writing");
    _rawFile.close();
    _buildState = BuildState::Failed;
    return false;
  }
  writeSectionFileHeader(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled, useEmbeddedStyle, bionicReadingEnabled, imageRendering);

  // Open .lut for writing (page offsets, appended per page)
  if (!Storage.openFileForWrite("SCT", _lutPath, _lutFile)) {
    LOG_ERR("SCT", "beginIncrementalBuild: failed to open .lut for writing");
    file.close();
    _rawFile.close();
    _buildState = BuildState::Failed;
    return false;
  }

  size_t lastSlash = localPath.find_last_of('/');
  const std::string contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";

  // Collect TOC anchors for this spine
  std::vector<std::string> tocAnchors;
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex >= 0) {
    for (int i = startTocIndex; i < epub->getTocItemsCount(); i++) {
      auto entry = epub->getTocItem(i);
      if (entry.spineIndex != spineIndex) break;
      if (!entry.anchor.empty()) tocAnchors.push_back(std::move(entry.anchor));
    }
  }

  CssParser* cssParser = nullptr;
  if (useEmbeddedStyle) {
    cssParser = epub->getCssParser();
    if (cssParser) {
      if (!cssParser->loadFromCache()) {
        LOG_ERR("SCT", "beginIncrementalBuild: failed to load CSS cache");
      }
      cssParser->resetResolveStats();
    }
  }

  // The page callback appends each page to .sec and its offset to .lut in real time.
  _parser = std::make_unique<ChapterHtmlSlimParser>(
      epub, renderer, fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth, viewportHeight,
      hyphenationEnabled, bionicReadingEnabled,
      [this](std::unique_ptr<Page> page) {
        const uint32_t position = file.position();
        if (page->serialize(file)) {
          lut.push_back(position);
          pageCount = static_cast<uint16_t>(lut.size());
          serialization::writePod(_lutFile, position);
          _lutFile.flush();
        } else {
          LOG_ERR("SCT", "incremental page callback: failed to serialize page %u", pageCount);
        }
      },
      useEmbeddedStyle, contentBase, _imagePath, imageRendering, std::move(tocAnchors), nullptr, cssParser);

  if (!_parser->setup(inflatedSize)) {
    LOG_ERR("SCT", "beginIncrementalBuild: parser setup failed");
    _parser.reset();
    file.close();
    _lutFile.close();
    _rawFile.close();
    _buildState = BuildState::Failed;
    return false;
  }

  Hyphenator::setPreferredLanguage(epub->getLanguage());
  _buildState = BuildState::InProgress;

  // Fire the initial burst — page 0 available before returning.
  pump(EpubIndexingPolicy::INITIAL_PAGES, EpubIndexingPolicy::INITIAL_MAX_MS);
  return _buildState != BuildState::Failed;
}

bool Section::pump(const uint8_t maxPages, const uint32_t maxMs) {
  if (_buildState != BuildState::InProgress || !_parser) {
    return false;
  }

  const uint32_t startMs = millis();
  _parser->setPageLimit(static_cast<int>(lut.size()) + maxPages);

  // Feed chunks from the .raw file into the parser until suspended, time budget hit, or EOF.
  constexpr size_t PUMP_CHUNK = 512;
  uint8_t chunkBuf[PUMP_CHUNK];

  if (_parser->isSuspended()) {
    // Resume — Expat will continue parsing from where it stopped within its current buffer.
    // Then we need to continue feeding SD data.
    _parser->resume();
  }

  while (_rawRemaining > 0 && !_parser->isSuspended()) {
    if (millis() - startMs >= maxMs) break;

    const size_t toRead = (_rawRemaining < PUMP_CHUNK) ? _rawRemaining : PUMP_CHUNK;
    const size_t bytesRead = _rawFile.read(chunkBuf, toRead);
    if (bytesRead == 0) {
      LOG_ERR("SCT", "pump: unexpected EOF in .raw after %u bytes remaining", static_cast<uint32_t>(_rawRemaining));
      break;
    }
    _rawRemaining -= bytesRead;
    _parser->write(chunkBuf, bytesRead);
  }

  LOG_DBG("SCT", "pump spine=%d pages=%u rawRemaining=%u suspended=%d elapsed=%ums", spineIndex, pageCount,
          static_cast<uint32_t>(_rawRemaining), _parser->isSuspended() ? 1 : 0,
          static_cast<uint32_t>(millis() - startMs));

  // If the raw file is exhausted and parser is not suspended, finalize
  if (_rawRemaining == 0 && !_parser->isSuspended()) {
    const bool finalizeOk = _parser->finalize();
    const bool streamOk = _parser->streamSucceeded();
    const bool success = finalizeOk && streamOk;

    // Capture anchors before destroying parser
    const auto anchors = _parser->getAnchors();
    const auto& paragraphLut = _parser->getParagraphLutPerPage();

    // Write .anc (anchor map + paragraph LUT)
    bool ancOk = false;
    {
      FsFile ancFile;
      if (Storage.openFileForWrite("SCT", _ancPath, ancFile)) {
        serialization::writePod(ancFile, static_cast<uint16_t>(anchors.size()));
        for (const auto& [anchor, page] : anchors) {
          serialization::writeString(ancFile, anchor);
          serialization::writePod(ancFile, page);
        }
        serialization::writePod(ancFile, static_cast<uint16_t>(paragraphLut.size()));
        for (const auto& entry : paragraphLut) {
          serialization::writePod(ancFile, entry.xhtmlByteOffset);
          serialization::writePod(ancFile, entry.paragraphIndex);
          serialization::writePod(ancFile, entry.listItemIndex);
        }
        ancFile.close();
        ancOk = true;
      } else {
        LOG_ERR("SCT", "pump: failed to write .anc");
      }
    }

    _lutFile.close();
    _parser.reset();
    _rawFile.close();
    Storage.remove(_rawPath.c_str());

    // Patch .sec header (parseComplete + placeholders for unused offsets)
    if (file.seek(header::kParseComplete)) {
      serialization::writePod(file, success && ancOk);
      serialization::writePod(file, pageCount);
      serialization::writePod(file, static_cast<uint32_t>(0));
      serialization::writePod(file, static_cast<uint32_t>(0));
      serialization::writePod(file, static_cast<uint32_t>(0));
      file.flush();
    }

    // Reopen .sec for reading
    file.close();
    if (!Storage.openFileForRead("SCT", _secPath, file)) {
      LOG_ERR("SCT", "pump: failed to reopen .sec for reading");
      _buildState = BuildState::Failed;
      return false;
    }

    if (ancOk) {
      buildTocBoundaries(anchors);
    }

    _buildState = (success && ancOk) ? BuildState::Complete : BuildState::Failed;
    LOG_DBG("SCT", "pump complete: spine=%d pages=%u state=%s", spineIndex, pageCount,
            _buildState == BuildState::Complete ? "complete" : "failed");
  }

  return _buildState != BuildState::Failed;
}

std::unique_ptr<Page> Section::loadPageFromSectionFile() {
  if (currentPage < 0 || currentPage >= static_cast<int>(lut.size())) {
    LOG_ERR("SCT", "loadPageFromSectionFile: page %d out of LUT range (%u entries)", currentPage,
            static_cast<uint32_t>(lut.size()));
    return nullptr;
  }

  if (!file) {
    // Safety fallback: file was closed unexpectedly; reopen.
    // Use _secPath when in incremental mode, filePath for legacy .bin format.
    const std::string& openPath = _secPath.empty() ? filePath : _secPath;
    LOG_ERR("SCT", "loadPageFromSectionFile: file not open, reopening %s", openPath.c_str());
    if (!Storage.openFileForRead("SCT", openPath, file)) {
      return nullptr;
    }
  }

  if (!file.seek(lut[currentPage])) {
    LOG_ERR("SCT", "loadPageFromSectionFile: seek to page %d offset %u failed", currentPage, lut[currentPage]);
    return nullptr;
  }
  return Page::deserialize(file);
  // File is intentionally NOT closed; stays open for the next page load
}

// Resolve TOC anchor-to-page mappings from the parser's in-memory anchor vector.
// Called after createSectionFile when anchors are already in memory.
// See buildTocBoundariesFromFile for the on-disk variant; the two are kept separate
// because the anchor resolution has fundamentally different iteration patterns
// (scan in-memory vector vs. stream from file with early exit).
void Section::buildTocBoundaries(const std::vector<std::pair<std::string, uint16_t>>& anchors) {
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex < 0) return;

  // Count TOC entries for this spine and how many have anchors to resolve
  const int tocCount = epub->getTocItemsCount();
  uint16_t totalEntries = 0;
  uint16_t unresolvedCount = 0;
  for (int i = startTocIndex; i < tocCount; i++) {
    const auto entry = epub->getTocItem(i);
    if (entry.spineIndex != spineIndex) break;
    totalEntries++;
    if (!entry.anchor.empty()) unresolvedCount++;
  }

  // If no TOC entries have anchors, all chapters start at page 0 and
  // getTocIndexForPage falls back to epub->getTocIndexForSpineIndex,
  // so there's nothing to resolve and no value in storing boundaries.
  if (totalEntries == 0 || unresolvedCount == 0) return;

  tocBoundaries.reserve(totalEntries);
  for (int i = startTocIndex; i < startTocIndex + totalEntries; i++) {
    const auto entry = epub->getTocItem(i);
    uint16_t page = 0;
    if (!entry.anchor.empty()) {
      for (const auto& [key, val] : anchors) {
        if (key == entry.anchor) {
          page = val;
          break;
        }
      }
    }
    tocBoundaries.push_back({i, page});
  }

  // Defensive sort in case TOC entries are out of document order in a malformed epub
  std::sort(tocBoundaries.begin(), tocBoundaries.end(),
            [](const TocBoundary& a, const TocBoundary& b) { return a.startPage < b.startPage; });
}

// Resolve TOC anchor-to-page mappings by scanning the section cache's on-disk anchor data.
// Called from loadSectionFile when anchors are not in memory. Caches the small set of
// TOC anchor strings first (since getTocItem does file I/O to BookMetadataCache), then
// streams through on-disk anchors matching only those, stopping as soon as all are found.
// See buildTocBoundaries for the in-memory variant.
void Section::buildTocBoundariesFromFile(FsFile& f) {
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex < 0) return;

  // Count TOC entries for this spine, then reserve and populate
  const int tocCount = epub->getTocItemsCount();
  uint16_t totalEntries = 0;
  uint16_t unresolvedCount = 0;
  for (int i = startTocIndex; i < tocCount; i++) {
    const auto entry = epub->getTocItem(i);
    if (entry.spineIndex != spineIndex) break;
    totalEntries++;
    if (!entry.anchor.empty()) unresolvedCount++;
  }

  // If no TOC entries have anchors, all chapters start at page 0 and
  // getTocIndexForPage falls back to epub->getTocIndexForSpineIndex,
  // so there's nothing to resolve and no value in storing boundaries.
  if (totalEntries == 0 || unresolvedCount == 0) return;

  // Cache TOC anchor strings before scanning disk, since getTocItem() does file I/O
  struct TocAnchorEntry {
    int tocIndex;
    std::string anchor;
  };
  std::vector<TocAnchorEntry> tocAnchorsToResolve;
  tocAnchorsToResolve.reserve(unresolvedCount);
  tocBoundaries.reserve(totalEntries);
  for (int i = startTocIndex; i < startTocIndex + totalEntries; i++) {
    const auto entry = epub->getTocItem(i);
    tocBoundaries.push_back({i, 0});
    if (!entry.anchor.empty()) {
      tocAnchorsToResolve.push_back({i, std::move(entry.anchor)});
    }
  }

  // Single pass through on-disk anchors, matching against cached TOC anchors.
  // Stop early once all TOC anchors are resolved.
  f.seek(header::kAnchorMap);
  uint32_t anchorMapOffset;
  serialization::readPod(f, anchorMapOffset);

  if (anchorMapOffset != 0) {
    f.seek(anchorMapOffset);
    uint16_t count;
    serialization::readPod(f, count);
    std::string key;
    for (uint16_t i = 0; i < count && unresolvedCount > 0; i++) {
      uint16_t page;
      serialization::readString(f, key);
      serialization::readPod(f, page);
      for (auto& tocAnchor : tocAnchorsToResolve) {
        if (!tocAnchor.anchor.empty() && key == tocAnchor.anchor) {
          tocBoundaries[tocAnchor.tocIndex - startTocIndex].startPage = page;
          tocAnchor.anchor.clear();  // mark resolved
          unresolvedCount--;
          break;
        }
      }
    }
  }

  // Defensive sort in case TOC entries are out of document order in a malformed epub
  std::sort(tocBoundaries.begin(), tocBoundaries.end(),
            [](const TocBoundary& a, const TocBoundary& b) { return a.startPage < b.startPage; });
}

int Section::getTocIndexForPage(const int page) const {
  if (tocBoundaries.empty()) {
    return epub->getTocIndexForSpineIndex(spineIndex);
  }

  // Find the first boundary AFTER page, then step back one
  auto it = std::upper_bound(tocBoundaries.begin(), tocBoundaries.end(), static_cast<uint16_t>(page),
                             [](uint16_t page, const TocBoundary& boundary) { return page < boundary.startPage; });
  if (it == tocBoundaries.begin()) {
    return tocBoundaries[0].tocIndex;
  }
  return std::prev(it)->tocIndex;
}

std::optional<int> Section::getPageForTocIndex(const int tocIndex) const {
  for (const auto& boundary : tocBoundaries) {
    if (boundary.tocIndex == tocIndex) {
      return boundary.startPage;
    }
  }
  return std::nullopt;
}

std::optional<Section::TocPageRange> Section::getPageRangeForTocIndex(const int tocIndex) const {
  for (size_t i = 0; i < tocBoundaries.size(); i++) {
    if (tocBoundaries[i].tocIndex == tocIndex) {
      const int startPage = tocBoundaries[i].startPage;
      const int endPage = (i + 1 < tocBoundaries.size()) ? static_cast<int>(tocBoundaries[i + 1].startPage) : pageCount;
      return TocPageRange{startPage, endPage};
    }
  }
  return std::nullopt;
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  // Incremental build: use .anc file which has same anchor map format at offset 0
  if (!_ancPath.empty()) {
    FsFile f;
    if (!Storage.openFileForRead("SCT", _ancPath, f)) return std::nullopt;
    uint16_t count;
    serialization::readPod(f, count);
    for (uint16_t i = 0; i < count; i++) {
      std::string key;
      uint16_t page;
      serialization::readString(f, key);
      serialization::readPod(f, page);
      if (key == anchor) {
        f.close();
        return page;
      }
    }
    f.close();
    return std::nullopt;
  }

  FsFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(header::kAnchorMap);
  uint32_t anchorMapOffset;
  serialization::readPod(f, anchorMapOffset);
  if (anchorMapOffset == 0 || anchorMapOffset >= fileSize) {
    f.close();
    return std::nullopt;
  }

  f.seek(anchorMapOffset);
  uint16_t count;
  serialization::readPod(f, count);
  for (uint16_t i = 0; i < count; i++) {
    std::string key;
    uint16_t page;
    serialization::readString(f, key);
    serialization::readPod(f, page);
    if (key == anchor) {
      f.close();
      return page;
    }
  }

  f.close();
  return std::nullopt;
}

bool Section::readParagraphLutHeader(FsFile& outFile, uint16_t& outCount, uint32_t& outLutStart) const {
  // Incremental build: paragraph LUT is in .anc after the anchor section
  if (!_ancPath.empty()) {
    if (!Storage.openFileForRead("SCT", _ancPath, outFile)) return false;
    uint16_t anchorCount;
    serialization::readPod(outFile, anchorCount);
    // Skip over anchor entries (string + uint16_t page each)
    for (uint16_t i = 0; i < anchorCount; i++) {
      std::string key;
      uint16_t page;
      serialization::readString(outFile, key);
      serialization::readPod(outFile, page);
    }
    serialization::readPod(outFile, outCount);
    if (outCount == 0) {
      outFile.close();
      return false;
    }
    const uint32_t fileSize = outFile.size();
    const uint64_t requiredBytes = static_cast<uint64_t>(outCount) * PARAGRAPH_LUT_ENTRY_SIZE;
    const uint32_t pos = outFile.position();
    if (static_cast<uint64_t>(fileSize) - pos < requiredBytes) {
      outFile.close();
      return false;
    }
    outLutStart = pos;
    return true;
  }

  if (!Storage.openFileForRead("SCT", filePath, outFile)) {
    return false;
  }

  const uint32_t fileSize = outFile.size();

  outFile.seek(header::kParagraphLut);
  uint32_t paragraphLutOffset;
  serialization::readPod(outFile, paragraphLutOffset);
  if (fileSize < sizeof(uint16_t) || paragraphLutOffset == 0 || paragraphLutOffset > fileSize - sizeof(uint16_t)) {
    outFile.close();
    return false;
  }

  outFile.seek(paragraphLutOffset);
  serialization::readPod(outFile, outCount);
  if (outCount == 0) {
    outFile.close();
    return false;
  }

  const uint64_t remainingBytes = static_cast<uint64_t>(fileSize) - paragraphLutOffset;
  const uint64_t requiredBytes = sizeof(uint16_t) + static_cast<uint64_t>(outCount) * PARAGRAPH_LUT_ENTRY_SIZE;
  if (remainingBytes < requiredBytes) {
    outFile.close();
    return false;
  }

  outLutStart = paragraphLutOffset + sizeof(uint16_t);

  return true;
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  FsFile f;
  uint16_t count = 0;
  uint32_t lutStart = 0;
  if (!readParagraphLutHeader(f, count, lutStart)) {
    return std::nullopt;
  }
  const uint32_t fileSize = f.size();

  // Each LUT entry stores the paragraph index at page-break time — i.e. the last
  // <p> whose start tag had been seen while page i was being laid out. Paragraph
  // P therefore first appears on the smallest i where storedPIdx[i] >= P.
  for (uint16_t i = 0; i < count; i++) {
    const uint32_t entryOffset = paragraphLutEntryOffset(lutStart, i) + sizeof(uint32_t);
    const uint64_t requiredOffset = static_cast<uint64_t>(entryOffset) + sizeof(uint16_t);
    if (requiredOffset > fileSize) {
      f.close();
      return std::nullopt;
    }
    f.seek(entryOffset);
    uint16_t pagePIdx;
    serialization::readPod(f, pagePIdx);
    if (pagePIdx >= pIndex) {
      f.close();
      return i;
    }
  }

  f.close();
  return static_cast<uint16_t>(count - 1);
}

std::optional<uint16_t> Section::getParagraphIndexForPage(const uint16_t page) const {
  FsFile f;
  uint16_t count = 0;
  uint32_t lutStart = 0;
  if (!readParagraphLutHeader(f, count, lutStart)) {
    return std::nullopt;
  }
  if (page >= count) {
    f.close();
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  const uint32_t entryOffset = paragraphLutEntryOffset(lutStart, page) + sizeof(uint32_t);
  const uint64_t requiredOffset = static_cast<uint64_t>(entryOffset) + sizeof(uint16_t);
  if (requiredOffset > fileSize) {
    f.close();
    return std::nullopt;
  }

  // Seek directly to the paragraphIndex field of the requested entry (skip xhtmlByteOffset)
  f.seek(entryOffset);
  uint16_t pIdx;
  serialization::readPod(f, pIdx);

  f.close();
  return pIdx;
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  if (liIndex == 0) {
    return std::nullopt;
  }

  FsFile f;
  uint16_t count = 0;
  uint32_t lutStart = 0;
  if (!readParagraphLutHeader(f, count, lutStart)) {
    return std::nullopt;
  }
  const uint32_t fileSize = f.size();

  // Mirror getPageForParagraphIndex: each entry stores the running li count at page-break
  // time, so the target li first appears on the smallest i where storedLiIdx[i] >= liIndex.
  // The listItemIndex field follows xhtmlByteOffset + paragraphIndex within each entry.
  for (uint16_t i = 0; i < count; i++) {
    const uint32_t entryOffset = paragraphLutEntryOffset(lutStart, i) + sizeof(uint32_t) + sizeof(uint16_t);
    const uint64_t requiredOffset = static_cast<uint64_t>(entryOffset) + sizeof(uint16_t);
    if (requiredOffset > fileSize) {
      f.close();
      return std::nullopt;
    }
    f.seek(entryOffset);
    uint16_t pageLiIdx;
    serialization::readPod(f, pageLiIdx);
    if (pageLiIdx >= liIndex) {
      f.close();
      return i;
    }
  }

  f.close();
  return static_cast<uint16_t>(count - 1);
}

std::optional<uint32_t> Section::getXhtmlByteOffsetForPage(const uint16_t page) const {
  FsFile f;
  uint16_t count = 0;
  uint32_t lutStart = 0;
  if (!readParagraphLutHeader(f, count, lutStart)) {
    return std::nullopt;
  }
  if (page >= count) {
    f.close();
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  const uint32_t entryOffset = paragraphLutEntryOffset(lutStart, page);
  const uint64_t requiredOffset = static_cast<uint64_t>(entryOffset) + sizeof(uint32_t);
  if (requiredOffset > fileSize) {
    f.close();
    return std::nullopt;
  }

  f.seek(entryOffset);
  uint32_t byteOffset;
  serialization::readPod(f, byteOffset);

  f.close();
  // A zero offset means the entry was recorded post-parse (last page), so it's unusable as a hint.
  return byteOffset > 0 ? std::optional<uint32_t>{byteOffset} : std::nullopt;
}
