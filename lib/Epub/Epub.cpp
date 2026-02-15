#include "Epub.h"

#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <ZipFile.h>

#include <algorithm>

#include "Epub/parsers/ContainerParser.h"
#include "Epub/parsers/ContentOpfParser.h"
#include "Epub/parsers/TocNavParser.h"
#include "Epub/parsers/TocNcxParser.h"

bool Epub::findContentOpfFile(std::string* contentOpfFile) const {
  const auto containerPath = "META-INF/container.xml";
  size_t containerSize;

  // Get file size without loading it all into heap
  if (!getItemSize(containerPath, &containerSize)) {
    LOG_ERR("EBP", "Could not find or size META-INF/container.xml");
    return false;
  }

  ContainerParser containerParser(containerSize);

  if (!containerParser.setup()) {
    return false;
  }

  // Stream read (reusing your existing stream logic)
  if (!readItemContentsToStream(containerPath, containerParser, 512)) {
    LOG_ERR("EBP", "Could not read META-INF/container.xml");
    return false;
  }

  // Extract the result
  if (containerParser.fullPath.empty()) {
    LOG_ERR("EBP", "Could not find valid rootfile in container.xml");
    return false;
  }

  *contentOpfFile = std::move(containerParser.fullPath);
  return true;
}

bool Epub::parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata) {
  std::string contentOpfFilePath;
  if (!findContentOpfFile(&contentOpfFilePath)) {
    LOG_ERR("EBP", "Could not find content.opf in zip");
    return false;
  }

  contentBasePath = contentOpfFilePath.substr(0, contentOpfFilePath.find_last_of('/') + 1);

  LOG_DBG("EBP", "Parsing content.opf: %s", contentOpfFilePath.c_str());

  size_t contentOpfSize;
  if (!getItemSize(contentOpfFilePath, &contentOpfSize)) {
    LOG_ERR("EBP", "Could not get size of content.opf");
    return false;
  }

  ContentOpfParser opfParser(getCachePath(), getBasePath(), contentOpfSize, bookMetadataCache.get());
  if (!opfParser.setup()) {
    LOG_ERR("EBP", "Could not setup content.opf parser");
    return false;
  }

  if (!readItemContentsToStream(contentOpfFilePath, opfParser, 1024)) {
    LOG_ERR("EBP", "Could not read content.opf");
    return false;
  }

  // Grab data from opfParser into epub
  bookMetadata.title = opfParser.title;
  bookMetadata.author = opfParser.author;
  bookMetadata.language = opfParser.language;
  bookMetadata.coverItemHref = opfParser.coverItemHref;
  bookMetadata.textReferenceHref = opfParser.textReferenceHref;

  if (!opfParser.tocNcxPath.empty()) {
    tocNcxItem = opfParser.tocNcxPath;
  }

  if (!opfParser.tocNavPath.empty()) {
    tocNavItem = opfParser.tocNavPath;
  }

  if (!opfParser.cssFiles.empty()) {
    cssFiles = opfParser.cssFiles;
  }

  LOG_DBG("EBP", "Successfully parsed content.opf");
  return true;
}

bool Epub::parseTocNcxFile() const {
  // the ncx file should have been specified in the content.opf file
  if (tocNcxItem.empty()) {
    LOG_DBG("EBP", "No ncx file specified");
    return false;
  }

  LOG_DBG("EBP", "Parsing toc ncx file: %s", tocNcxItem.c_str());

  const auto tmpNcxPath = getCachePath() + "/toc.ncx";
  FsFile tempNcxFile;
  if (!Storage.openFileForWrite("EBP", tmpNcxPath, tempNcxFile)) {
    return false;
  }
  readItemContentsToStream(tocNcxItem, tempNcxFile, 1024);
  tempNcxFile.close();
  if (!Storage.openFileForRead("EBP", tmpNcxPath, tempNcxFile)) {
    return false;
  }
  const auto ncxSize = tempNcxFile.size();

  TocNcxParser ncxParser(contentBasePath, ncxSize, bookMetadataCache.get());

  if (!ncxParser.setup()) {
    LOG_ERR("EBP", "Could not setup toc ncx parser");
    tempNcxFile.close();
    return false;
  }

  const auto ncxBuffer = static_cast<uint8_t*>(malloc(1024));
  if (!ncxBuffer) {
    LOG_ERR("EBP", "Could not allocate memory for toc ncx parser");
    tempNcxFile.close();
    return false;
  }

  while (tempNcxFile.available()) {
    const auto readSize = tempNcxFile.read(ncxBuffer, 1024);
    if (readSize == 0) break;
    const auto processedSize = ncxParser.write(ncxBuffer, readSize);

    if (processedSize != readSize) {
      LOG_ERR("EBP", "Could not process all toc ncx data");
      free(ncxBuffer);
      tempNcxFile.close();
      return false;
    }
  }

  free(ncxBuffer);
  tempNcxFile.close();
  Storage.remove(tmpNcxPath.c_str());

  LOG_DBG("EBP", "Parsed TOC items");
  return true;
}

bool Epub::parseTocNavFile() const {
  // the nav file should have been specified in the content.opf file (EPUB 3)
  if (tocNavItem.empty()) {
    LOG_DBG("EBP", "No nav file specified");
    return false;
  }

  LOG_DBG("EBP", "Parsing toc nav file: %s", tocNavItem.c_str());

  const auto tmpNavPath = getCachePath() + "/toc.nav";
  FsFile tempNavFile;
  if (!Storage.openFileForWrite("EBP", tmpNavPath, tempNavFile)) {
    return false;
  }
  readItemContentsToStream(tocNavItem, tempNavFile, 1024);
  tempNavFile.close();
  if (!Storage.openFileForRead("EBP", tmpNavPath, tempNavFile)) {
    return false;
  }
  const auto navSize = tempNavFile.size();

  // Note: We can't use `contentBasePath` here as the nav file may be in a different folder to the content.opf
  // and the HTMLX nav file will have hrefs relative to itself
  const std::string navContentBasePath = tocNavItem.substr(0, tocNavItem.find_last_of('/') + 1);
  TocNavParser navParser(navContentBasePath, navSize, bookMetadataCache.get());

  if (!navParser.setup()) {
    LOG_ERR("EBP", "Could not setup toc nav parser");
    return false;
  }

  const auto navBuffer = static_cast<uint8_t*>(malloc(1024));
  if (!navBuffer) {
    LOG_ERR("EBP", "Could not allocate memory for toc nav parser");
    return false;
  }

  while (tempNavFile.available()) {
    const auto readSize = tempNavFile.read(navBuffer, 1024);
    const auto processedSize = navParser.write(navBuffer, readSize);

    if (processedSize != readSize) {
      LOG_ERR("EBP", "Could not process all toc nav data");
      free(navBuffer);
      tempNavFile.close();
      return false;
    }
  }

  free(navBuffer);
  tempNavFile.close();
  Storage.remove(tmpNavPath.c_str());

  LOG_DBG("EBP", "Parsed TOC nav items");
  return true;
}

void Epub::parseCssFiles() const {
  if (cssFiles.empty()) {
    LOG_DBG("EBP", "No CSS files to parse, but CssParser created for inline styles");
  }

  // See if we have a cached version of the CSS rules
  if (!cssParser->hasCache()) {
    // No cache yet - parse CSS files
    for (const auto& cssPath : cssFiles) {
      LOG_DBG("EBP", "Parsing CSS file: %s", cssPath.c_str());

      // Extract CSS file to temp location
      const auto tmpCssPath = getCachePath() + "/.tmp.css";
      FsFile tempCssFile;
      if (!Storage.openFileForWrite("EBP", tmpCssPath, tempCssFile)) {
        LOG_ERR("EBP", "Could not create temp CSS file");
        continue;
      }
      if (!readItemContentsToStream(cssPath, tempCssFile, 1024)) {
        LOG_ERR("EBP", "Could not read CSS file: %s", cssPath.c_str());
        tempCssFile.close();
        Storage.remove(tmpCssPath.c_str());
        continue;
      }
      tempCssFile.close();

      // Parse the CSS file
      if (!Storage.openFileForRead("EBP", tmpCssPath, tempCssFile)) {
        LOG_ERR("EBP", "Could not open temp CSS file for reading");
        Storage.remove(tmpCssPath.c_str());
        continue;
      }
      cssParser->loadFromStream(tempCssFile);
      tempCssFile.close();
      Storage.remove(tmpCssPath.c_str());
    }

    // Save to cache for next time
    if (!cssParser->saveToCache()) {
      LOG_ERR("EBP", "Failed to save CSS rules to cache");
    }
    cssParser->clear();

    LOG_DBG("EBP", "Loaded %zu CSS style rules from %zu files", cssParser->ruleCount(), cssFiles.size());
  }
}

// load in the meta data for the epub file
bool Epub::load(const bool buildIfMissing, const bool skipLoadingCss) {
  LOG_DBG("EBP", "Loading ePub: %s", filepath.c_str());

  // Initialize spine/TOC cache
  bookMetadataCache.reset(new BookMetadataCache(cachePath));
  // Always create CssParser - needed for inline style parsing even without CSS files
  cssParser.reset(new CssParser(cachePath));

  // Try to load existing cache first
  if (bookMetadataCache->load()) {
    if (!skipLoadingCss && !cssParser->hasCache()) {
      LOG_DBG("EBP", "Warning: CSS rules cache not found, attempting to parse CSS files");
      // to get CSS file list
      if (!parseContentOpf(bookMetadataCache->coreMetadata)) {
        LOG_ERR("EBP", "Could not parse content.opf from cached bookMetadata for CSS files");
        // continue anyway - book will work without CSS and we'll still load any inline style CSS
      }
      parseCssFiles();
    }
    LOG_DBG("EBP", "Loaded ePub: %s", filepath.c_str());
    return true;
  }

  // If we didn't load from cache above and we aren't allowed to build, fail now
  if (!buildIfMissing) {
    return false;
  }

  // Cache doesn't exist or is invalid, build it
  LOG_DBG("EBP", "Cache not found, building spine/TOC cache");
  setupCacheDir();

  const uint32_t indexingStart = millis();

  // Begin building cache - stream entries to disk immediately
  if (!bookMetadataCache->beginWrite()) {
    LOG_ERR("EBP", "Could not begin writing cache");
    return false;
  }

  // OPF Pass
  const uint32_t opfStart = millis();
  BookMetadataCache::BookMetadata bookMetadata;
  if (!bookMetadataCache->beginContentOpfPass()) {
    LOG_ERR("EBP", "Could not begin writing content.opf pass");
    return false;
  }
  if (!parseContentOpf(bookMetadata)) {
    LOG_ERR("EBP", "Could not parse content.opf");
    return false;
  }
  if (!bookMetadataCache->endContentOpfPass()) {
    LOG_ERR("EBP", "Could not end writing content.opf pass");
    return false;
  }
  LOG_DBG("EBP", "OPF pass completed in %lu ms", millis() - opfStart);

  // TOC Pass - try EPUB 3 nav first, fall back to NCX
  const uint32_t tocStart = millis();
  if (!bookMetadataCache->beginTocPass()) {
    LOG_ERR("EBP", "Could not begin writing toc pass");
    return false;
  }

  bool tocParsed = false;

  // Try EPUB 3 nav document first (preferred)
  if (!tocNavItem.empty()) {
    LOG_DBG("EBP", "Attempting to parse EPUB 3 nav document");
    tocParsed = parseTocNavFile();
  }

  // Fall back to NCX if nav parsing failed or wasn't available
  if (!tocParsed && !tocNcxItem.empty()) {
    LOG_DBG("EBP", "Falling back to NCX TOC");
    tocParsed = parseTocNcxFile();
  }

  if (!tocParsed) {
    LOG_ERR("EBP", "Warning: Could not parse any TOC format");
    // Continue anyway - book will work without TOC
  }

  if (!bookMetadataCache->endTocPass()) {
    LOG_ERR("EBP", "Could not end writing toc pass");
    return false;
  }
  LOG_DBG("EBP", "TOC pass completed in %lu ms", millis() - tocStart);

  // Close the cache files
  if (!bookMetadataCache->endWrite()) {
    LOG_ERR("EBP", "Could not end writing cache");
    return false;
  }

  // Build final book.bin
  const uint32_t buildStart = millis();
  if (!bookMetadataCache->buildBookBin(filepath, bookMetadata)) {
    LOG_ERR("EBP", "Could not update mappings and sizes");
    return false;
  }
  LOG_DBG("EBP", "buildBookBin completed in %lu ms", millis() - buildStart);
  LOG_DBG("EBP", "Total indexing completed in %lu ms", millis() - indexingStart);

  if (!bookMetadataCache->cleanupTmpFiles()) {
    LOG_DBG("EBP", "Could not cleanup tmp files - ignoring");
  }

  // Reload the cache from disk so it's in the correct state
  bookMetadataCache.reset(new BookMetadataCache(cachePath));
  if (!bookMetadataCache->load()) {
    LOG_ERR("EBP", "Failed to reload cache after writing");
    return false;
  }

  if (!skipLoadingCss) {
    // Parse CSS files after cache reload
    parseCssFiles();
  }

  LOG_DBG("EBP", "Loaded ePub: %s", filepath.c_str());
  return true;
}

bool Epub::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("EPB", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("EPB", "Failed to clear cache");
    return false;
  }

  LOG_DBG("EPB", "Cache cleared successfully");
  return true;
}

void Epub::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  Storage.mkdir(cachePath.c_str());
}

const std::string& Epub::getCachePath() const { return cachePath; }

const std::string& Epub::getPath() const { return filepath; }

const std::string& Epub::getTitle() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.title;
}

const std::string& Epub::getAuthor() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.author;
}

const std::string& Epub::getLanguage() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.language;
}

std::string Epub::getCoverBmpPath(bool cropped) const {
  const auto coverFileName = std::string("cover") + (cropped ? "_crop" : "");
  return cachePath + "/" + coverFileName + ".bmp";
}

bool Epub::generateCoverBmp(bool cropped) const {
  bool invalid = false;
  // Already generated, return true
  if (Storage.exists(getCoverBmpPath(cropped).c_str())) {
    // is this a valid cover or just an empty file we created to mark generation attempts?
    invalid = !isValidThumbnailBmp(getCoverBmpPath(cropped));
    if (invalid) {
      // Remove the old invalid cover so we can attempt to generate a new one
      Storage.remove(getCoverBmpPath(cropped).c_str());
      LOG_ERR("EBP", "Previous cover generation attempt failed for %s mode, retrying", cropped ? "cropped" : "fit");
    } else {
      return true;
    }
  }

  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "Cannot generate cover BMP, cache not loaded");
    return false;
  }

  const auto coverImageHref = bookMetadataCache->coreMetadata.coverItemHref;
  std::string effectiveCoverImageHref = coverImageHref;
  if (coverImageHref.empty()) {
    // Fallback: try common cover filenames
    std::vector<std::string> coverCandidates = getCoverCandidates();
    for (const auto& candidate : coverCandidates) {
      effectiveCoverImageHref = candidate;
      // Try to read a small amount to check if exists
      uint8_t* test = readItemContentsToBytes(candidate, nullptr, false);
      if (test) {
        free(test);
        break;
      } else {
        effectiveCoverImageHref.clear();
      }
    }
  }
  if (effectiveCoverImageHref.empty()) {
    LOG_ERR("EBP", "No known cover image");
    return false;
  }

  // Check for JPG/JPEG extensions (case insensitive)
  std::string lowerHref = effectiveCoverImageHref;
  std::transform(lowerHref.begin(), lowerHref.end(), lowerHref.begin(), ::tolower);
  bool isJpg =
      lowerHref.substr(lowerHref.length() - 4) == ".jpg" || lowerHref.substr(lowerHref.length() - 5) == ".jpeg";
  if (isJpg) {
    LOG_DBG("EBP", "Generating BMP from JPG cover image (%s mode)", cropped ? "cropped" : "fit");
    const auto coverJpgTempPath = getCachePath() + "/.cover.jpg";

    FsFile coverJpg;
    if (!Storage.openFileForWrite("EBP", coverJpgTempPath, coverJpg)) {
      return false;
    }
    readItemContentsToStream(effectiveCoverImageHref, coverJpg, 1024);
    coverJpg.close();

    if (!Storage.openFileForRead("EBP", coverJpgTempPath, coverJpg)) {
      return false;
    }

    FsFile coverBmp;
    if (!Storage.openFileForWrite("EBP", getCoverBmpPath(cropped), coverBmp)) {
      coverJpg.close();
      return false;
    }
    const bool success = JpegToBmpConverter::jpegFileToBmpStream(coverJpg, coverBmp, cropped);
    coverJpg.close();
    coverBmp.close();
    Storage.remove(coverJpgTempPath.c_str());

    if (!success) {
      LOG_ERR("EBP", "Failed to generate BMP from JPG cover image");
      // Instead of removing the file, create a dummy cover with X pattern
      coverBmp.close();
      Storage.remove(coverJpgTempPath.c_str());
      return generateInvalidFormatCoverBmp(cropped);
    }
    LOG_DBG("EBP", "Generated BMP from cover image, success: %s", success ? "yes" : "no");
    return success;
  } else {
    LOG_ERR("EBP", "Cover image is not a supported format, skipping");
    // Create a dummy cover to indicate unsupported format
    return generateInvalidFormatCoverBmp(cropped);
  }

  return false;
}

std::string Epub::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }
std::string Epub::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }

bool Epub::generateThumbBmp(int height) const {
  bool invalid = false;
  // Already generated, return true
  if (Storage.exists(getThumbBmpPath(height).c_str())) {
    // is this a valid thumbnail or just an empty file we created to mark generation attempts?
    invalid = !isValidThumbnailBmp(getThumbBmpPath(height));
    if (invalid) {
      // Remove the old invalid thumbnail so we can attempt to generate a new one
      Storage.remove(getThumbBmpPath(height).c_str());
      LOG_DBG("EBP", "Previous thumbnail generation attempt failed for height %d, skipping", height);
    } else {
      // LOG_DBG("EBP", "Thumbnail BMP already exists for height %d", height);
      return true;
    }
  }

  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "Cannot generate thumb BMP, cache not loaded");
    return false;
  }

  const auto coverImageHref = bookMetadataCache->coreMetadata.coverItemHref;
  std::string effectiveCoverImageHref = coverImageHref;
  if (coverImageHref.empty()) {
    // Fallback: try common cover filenames
    std::vector<std::string> coverCandidates = getCoverCandidates();
    for (const auto& candidate : coverCandidates) {
      effectiveCoverImageHref = candidate;
      // Try to read a small amount to check if exists
      uint8_t* test = readItemContentsToBytes(candidate, nullptr, false);
      if (test) {
        free(test);
        break;
      } else {
        effectiveCoverImageHref.clear();
      }
    }
  }
  if (effectiveCoverImageHref.empty()) {
    LOG_DBG("EBP", "No known cover image for thumbnail");
  } else {
    // Check for JPG/JPEG extensions (case insensitive)
    std::string lowerHref = effectiveCoverImageHref;
    std::transform(lowerHref.begin(), lowerHref.end(), lowerHref.begin(), ::tolower);
    bool isJpg =
        lowerHref.substr(lowerHref.length() - 4) == ".jpg" || lowerHref.substr(lowerHref.length() - 5) == ".jpeg";
    if (isJpg) {
      LOG_DBG("EBP", "Generating thumb BMP from JPG cover image");
      const auto coverJpgTempPath = getCachePath() + "/.cover.jpg";

      FsFile coverJpg;
      if (!Storage.openFileForWrite("EBP", coverJpgTempPath, coverJpg)) {
        return false;
      }
      readItemContentsToStream(effectiveCoverImageHref, coverJpg, 1024);
      coverJpg.close();

      if (!Storage.openFileForRead("EBP", coverJpgTempPath, coverJpg)) {
        return false;
      }

      FsFile thumbBmp;
      if (!Storage.openFileForWrite("EBP", getThumbBmpPath(height), thumbBmp)) {
        coverJpg.close();
        return false;
      }
      // Use smaller target size for Continue Reading card (half of screen: 240x400)
      // Generate 1-bit BMP for fast home screen rendering (no gray passes needed)
      int THUMB_TARGET_WIDTH = height * 0.6;
      int THUMB_TARGET_HEIGHT = height;
      const bool success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(coverJpg, thumbBmp, THUMB_TARGET_WIDTH,
                                                                               THUMB_TARGET_HEIGHT);
      coverJpg.close();
      thumbBmp.close();
      Storage.remove(coverJpgTempPath.c_str());

      if (!success) {
        LOG_ERR("EBP", "Failed to generate thumb BMP from JPG cover image");
        // Instead of removing the file, create a dummy thumbnail with X pattern
        thumbBmp.close();
        Storage.remove(coverJpgTempPath.c_str());
        return generateInvalidFormatThumbBmp(height);
      }
      LOG_DBG("EBP", "Generated thumb BMP from JPG cover image, success: %s", success ? "yes" : "no");
      return success;
    } else {
      LOG_ERR("EBP", "Cover image is not a JPG, creating invalid format thumbnail");
      // Create a dummy thumbnail to indicate unsupported format
      return generateInvalidFormatThumbBmp(height);
    }
  }

  // Write an empty bmp file to avoid generation attempts in the future
  FsFile thumbBmp;
  Storage.openFileForWrite("EBP", getThumbBmpPath(height), thumbBmp);
  thumbBmp.close();
  return false;
}

bool Epub::generateInvalidFormatThumbBmp(int height) const {
  // Create a simple 1-bit BMP with an X pattern to indicate invalid format.
  // This BMP is a valid 1-bit file used as a marker to prevent repeated
  // generation attempts when conversion fails (e.g., progressive JPG).
  const int width = height * 0.6;                // Same aspect ratio as normal thumbnails
  const int rowBytes = ((width + 31) / 32) * 4;  // 1-bit rows padded to 4-byte boundary
  const int imageSize = rowBytes * height;
  const int fileSize = 14 + 40 + 8 + imageSize;  // Header + DIB + palette + data
  const int dataOffset = 14 + 40 + 8;

  FsFile thumbBmp;
  if (!Storage.openFileForWrite("EBP", getThumbBmpPath(height), thumbBmp)) {
    return false;
  }

  // BMP file header (14 bytes)
  thumbBmp.write('B');
  thumbBmp.write('M');
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  uint32_t reserved = 0;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&reserved), 4);
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&dataOffset), 4);

  // DIB header (BITMAPINFOHEADER - 40 bytes)
  uint32_t dibHeaderSize = 40;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&dibHeaderSize), 4);
  int32_t bmpWidth = width;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&bmpWidth), 4);
  int32_t bmpHeight = -height;  // Negative for top-down
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&bmpHeight), 4);
  uint16_t planes = 1;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  uint16_t bitsPerPixel = 1;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&bitsPerPixel), 2);
  uint32_t compression = 0;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&compression), 4);
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&imageSize), 4);
  int32_t ppmX = 2835;  // 72 DPI
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&ppmX), 4);
  int32_t ppmY = 2835;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&ppmY), 4);
  uint32_t colorsUsed = 2;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&colorsUsed), 4);
  uint32_t colorsImportant = 2;
  thumbBmp.write(reinterpret_cast<const uint8_t*>(&colorsImportant), 4);

  // Color palette (2 colors for 1-bit)
  uint8_t black[4] = {0x00, 0x00, 0x00, 0x00};  // Color 0: Black
  thumbBmp.write(black, 4);
  uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0x00};  // Color 1: White
  thumbBmp.write(white, 4);

  // Generate X pattern bitmap data
  // In BMP, 0 = black (first color in palette), 1 = white
  // We'll draw black pixels on white background
  for (int y = 0; y < height; y++) {
    std::vector<uint8_t> rowData(rowBytes, 0xFF);  // Initialize to all white (1s)

    // Map this row to a horizontal position for diagonals
    const int scaledY = (y * width) / height;
    const int thickness = 2;  // thickness of diagonal lines in pixels

    for (int x = 0; x < width; x++) {
      bool drawPixel = false;
      // Main diagonal (top-left to bottom-right)
      if (std::abs(x - scaledY) <= thickness) drawPixel = true;
      // Other diagonal (top-right to bottom-left)
      if (std::abs(x - (width - 1 - scaledY)) <= thickness) drawPixel = true;

      if (drawPixel) {
        const int byteIndex = x / 8;
        const int bitIndex = 7 - (x % 8);  // MSB first
        rowData[byteIndex] &= static_cast<uint8_t>(~(1 << bitIndex));
      }
    }

    // Write the row data
    thumbBmp.write(rowData.data(), rowBytes);
  }

  thumbBmp.close();
  LOG_DBG("EBP", "Generated invalid format thumbnail BMP");
  return true;
}

bool Epub::generateInvalidFormatCoverBmp(bool cropped) const {
  // Create a simple 1-bit BMP with an X pattern to indicate invalid format.
  // This BMP is intentionally a valid image that visually indicates a
  // malformed/unsupported cover image instead of leaving an empty marker
  // file that would cause repeated generation attempts.
  // Derive logical portrait dimensions from the display hardware constants
  // EInkDisplay reports native panel orientation as 800x480; use min/max
  const int hwW = HalDisplay::DISPLAY_WIDTH;
  const int hwH = HalDisplay::DISPLAY_HEIGHT;
  const int width = std::min(hwW, hwH);          // logical portrait width (480)
  const int height = std::max(hwW, hwH);         // logical portrait height (800)
  const int rowBytes = ((width + 31) / 32) * 4;  // 1-bit rows padded to 4-byte boundary
  const int imageSize = rowBytes * height;
  const int fileSize = 14 + 40 + 8 + imageSize;  // Header + DIB + palette + data
  const int dataOffset = 14 + 40 + 8;

  FsFile coverBmp;
  if (!Storage.openFileForWrite("EBP", getCoverBmpPath(cropped), coverBmp)) {
    return false;
  }

  // BMP file header (14 bytes)
  coverBmp.write('B');
  coverBmp.write('M');
  coverBmp.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  uint32_t reserved = 0;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&reserved), 4);
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dataOffset), 4);

  // DIB header (BITMAPINFOHEADER - 40 bytes)
  uint32_t dibHeaderSize = 40;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&dibHeaderSize), 4);
  int32_t bmpWidth = width;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&bmpWidth), 4);
  int32_t bmpHeight = -height;  // Negative for top-down
  coverBmp.write(reinterpret_cast<const uint8_t*>(&bmpHeight), 4);
  uint16_t planes = 1;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  uint16_t bitsPerPixel = 1;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&bitsPerPixel), 2);
  uint32_t compression = 0;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&compression), 4);
  coverBmp.write(reinterpret_cast<const uint8_t*>(&imageSize), 4);
  int32_t ppmX = 2835;  // 72 DPI
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmX), 4);
  int32_t ppmY = 2835;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&ppmY), 4);
  uint32_t colorsUsed = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsUsed), 4);
  uint32_t colorsImportant = 2;
  coverBmp.write(reinterpret_cast<const uint8_t*>(&colorsImportant), 4);

  // Color palette (2 colors for 1-bit)
  uint8_t black[4] = {0x00, 0x00, 0x00, 0x00};  // Color 0: Black
  coverBmp.write(black, 4);
  uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0x00};  // Color 1: White
  coverBmp.write(white, 4);

  // Generate X pattern bitmap data
  // In BMP, 0 = black (first color in palette), 1 = white
  // We'll draw black pixels on white background
  for (int y = 0; y < height; y++) {
    std::vector<uint8_t> rowData(rowBytes, 0xFF);  // Initialize to all white (1s)

    const int scaledY = (y * width) / height;
    const int thickness = 6;  // thicker lines for full-cover visibility

    for (int x = 0; x < width; x++) {
      bool drawPixel = false;
      if (std::abs(x - scaledY) <= thickness) drawPixel = true;
      if (std::abs(x - (width - 1 - scaledY)) <= thickness) drawPixel = true;

      if (drawPixel) {
        const int byteIndex = x / 8;
        const int bitIndex = 7 - (x % 8);
        rowData[byteIndex] &= static_cast<uint8_t>(~(1 << bitIndex));
      }
    }

    coverBmp.write(rowData.data(), rowBytes);
  }

  coverBmp.close();
  LOG_DBG("EBP", "Generated invalid format cover BMP");
  return true;
}

std::vector<std::string> Epub::getCoverCandidates() const {
  std::vector<std::string> coverDirectories = {".", "images", "Images", "OEBPS", "OEBPS/images", "OEBPS/Images"};
  std::vector<std::string> coverExtensions = {".jpg", ".jpeg"};  // add ".png" when PNG cover support is implemented
  std::vector<std::string> coverCandidates;
  for (const auto& ext : coverExtensions) {
    for (const auto& dir : coverDirectories) {
      std::string candidate = (dir == ".") ? "cover" + ext : dir + "/cover" + ext;
      coverCandidates.push_back(candidate);
    }
  }
  return coverCandidates;
}

uint8_t* Epub::readItemContentsToBytes(const std::string& itemHref, size_t* size, const bool trailingNullByte) const {
  if (itemHref.empty()) {
    LOG_DBG("EBP", "Failed to read item, empty href");
    return nullptr;
  }

  const std::string path = FsHelpers::normalisePath(itemHref);

  const auto content = ZipFile(filepath).readFileToMemory(path.c_str(), size, trailingNullByte);
  if (!content) {
    LOG_DBG("EBP", "Failed to read item %s", path.c_str());
    return nullptr;
  }

  return content;
}

bool Epub::readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize) const {
  if (itemHref.empty()) {
    LOG_DBG("EBP", "Failed to read item, empty href");
    return false;
  }

  const std::string path = FsHelpers::normalisePath(itemHref);
  return ZipFile(filepath).readFileToStream(path.c_str(), out, chunkSize);
}

bool Epub::getItemSize(const std::string& itemHref, size_t* size) const {
  const std::string path = FsHelpers::normalisePath(itemHref);
  return ZipFile(filepath).getInflatedFileSize(path.c_str(), size);
}

int Epub::getSpineItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }
  return bookMetadataCache->getSpineCount();
}

size_t Epub::getCumulativeSpineItemSize(const int spineIndex) const { return getSpineItem(spineIndex).cumulativeSize; }

BookMetadataCache::SpineEntry Epub::getSpineItem(const int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineItem called but cache not loaded");
    return {};
  }

  if (spineIndex < 0 || spineIndex >= bookMetadataCache->getSpineCount()) {
    LOG_ERR("EBP", "getSpineItem index:%d is out of range", spineIndex);
    return bookMetadataCache->getSpineEntry(0);
  }

  return bookMetadataCache->getSpineEntry(spineIndex);
}

BookMetadataCache::TocEntry Epub::getTocItem(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_DBG("EBP", "getTocItem called but cache not loaded");
    return {};
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_DBG("EBP", "getTocItem index:%d is out of range", tocIndex);
    return {};
  }

  return bookMetadataCache->getTocEntry(tocIndex);
}

int Epub::getTocItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }

  return bookMetadataCache->getTocCount();
}

// work out the section index for a toc index
int Epub::getSpineIndexForTocIndex(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineIndexForTocIndex called but cache not loaded");
    return 0;
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_ERR("EBP", "getSpineIndexForTocIndex: tocIndex %d out of range", tocIndex);
    return 0;
  }

  const int spineIndex = bookMetadataCache->getTocEntry(tocIndex).spineIndex;
  if (spineIndex < 0) {
    LOG_DBG("EBP", "Section not found for TOC index %d", tocIndex);
    return 0;
  }

  return spineIndex;
}

int Epub::getTocIndexForSpineIndex(const int spineIndex) const { return getSpineItem(spineIndex).tocIndex; }

size_t Epub::getBookSize() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || bookMetadataCache->getSpineCount() == 0) {
    return 0;
  }
  return getCumulativeSpineItemSize(getSpineItemsCount() - 1);
}

int Epub::getSpineIndexForTextReference() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineIndexForTextReference called but cache not loaded");
    return 0;
  }
  LOG_DBG("EBP", "Core Metadata: cover(%d)=%s, textReference(%d)=%s",
          bookMetadataCache->coreMetadata.coverItemHref.size(), bookMetadataCache->coreMetadata.coverItemHref.c_str(),
          bookMetadataCache->coreMetadata.textReferenceHref.size(),
          bookMetadataCache->coreMetadata.textReferenceHref.c_str());

  if (bookMetadataCache->coreMetadata.textReferenceHref.empty()) {
    // there was no textReference in epub, so we return 0 (the first chapter)
    return 0;
  }

  // loop through spine items to get the correct index matching the text href
  for (size_t i = 0; i < getSpineItemsCount(); i++) {
    if (getSpineItem(i).href == bookMetadataCache->coreMetadata.textReferenceHref) {
      LOG_DBG("EBP", "Text reference %s found at index %d", bookMetadataCache->coreMetadata.textReferenceHref.c_str(),
              i);
      return i;
    }
  }
  // This should not happen, as we checked for empty textReferenceHref earlier
  LOG_DBG("EBP", "Section not found for text reference");
  return 0;
}

// Calculate progress in book (returns 0.0-1.0)
float Epub::calculateProgress(const int currentSpineIndex, const float currentSpineRead) const {
  const size_t bookSize = getBookSize();
  if (bookSize == 0) {
    return 0.0f;
  }
  const size_t prevChapterSize = (currentSpineIndex >= 1) ? getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
  const size_t curChapterSize = getCumulativeSpineItemSize(currentSpineIndex) - prevChapterSize;
  const float sectionProgSize = currentSpineRead * static_cast<float>(curChapterSize);
  const float totalProgress = static_cast<float>(prevChapterSize) + sectionProgSize;
  return totalProgress / static_cast<float>(bookSize);
}

bool Epub::isValidThumbnailBmp(const std::string& bmpPath) {
  if (!Storage.exists(bmpPath.c_str())) {
    LOG_DBG("EBP", "Thumbnail BMP does not exist at path: %s", bmpPath.c_str());
    return false;
  }
  FsFile file = Storage.open(bmpPath.c_str());
  if (!file) {
    LOG_ERR("EBP", "Failed to open Thumbnail BMP at path: %s", bmpPath.c_str());
    return false;
  }
  size_t fileSize = file.size();
  if (fileSize == 0) {
    // Empty file is a marker for "no cover available"
    LOG_DBG("EBP", "Thumbnail BMP is empty (no cover marker) at path: %s", bmpPath.c_str());
    file.close();
    return false;
  }
  // BMP header starts with 'B' 'M'
  uint8_t header[2];
  size_t bytesRead = file.read(header, 2);
  if (bytesRead != 2) {
    LOG_ERR("EBP", "Failed to read Thumbnail BMP header at path: %s", bmpPath.c_str());
    file.close();
    return false;
  }
  LOG_DBG("EBP", "Thumbnail BMP header: %c%c", header[0], header[1]);
  file.close();
  return header[0] == 'B' && header[1] == 'M';
}
