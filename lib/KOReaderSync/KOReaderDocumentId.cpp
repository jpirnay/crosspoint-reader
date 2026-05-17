#include "KOReaderDocumentId.h"

#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>

#include <functional>

namespace {
// Extract filename from path (everything after last '/')
std::string getFilename(const std::string& path) {
  const size_t pos = path.rfind('/');
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
}
}  // namespace

std::string KOReaderDocumentId::getCacheFilePath(const std::string& filePath) {
  // Mirror the Epub cache directory convention so the hash file shares the
  // same per-book folder as other cached data.
  return std::string("/.crosspoint/epub_") + std::to_string(std::hash<std::string>{}(filePath)) + "/koreader_docid.txt";
}

std::string KOReaderDocumentId::loadCachedHash(const std::string& cacheFilePath, const size_t fileSize,
                                               const std::string& currentFingerprint) {
  if (!Storage.exists(cacheFilePath.c_str())) {
    return "";
  }

  // Format: "<filesize>:<fingerprint>\n<32-char-hex-hash>"
  // Max size: ~20 digits + ':' + 8 hex + '\n' + 32 hex + '\n' = ~64 bytes
  char buf[128];
  if (Storage.readFileToBuffer(cacheFilePath.c_str(), buf, sizeof(buf)) == 0) {
    return "";
  }

  char* const newline = strchr(buf, '\n');
  if (!newline) {
    return "";
  }
  *newline = '\0';  // split header / hash in-place

  const char* const colon = strchr(buf, ':');
  if (!colon) {
    LOG_DBG("KODoc", "Hash cache invalidated: header missing fingerprint");
    return "";
  }

  // Validate and parse the size token (everything before the colon)
  for (const char* p = buf; p < colon; ++p) {
    if (*p < '0' || *p > '9') {
      LOG_DBG("KODoc", "Hash cache invalidated: size token not numeric ('%s')", buf);
      return "";
    }
  }
  char* end;
  const unsigned long parsedSize = strtoul(buf, &end, 10);
  if (end != colon || parsedSize != static_cast<unsigned long>(fileSize)) {
    LOG_DBG("KODoc", "Hash cache invalidated: file size changed (%lu -> %zu)", parsedSize, fileSize);
    return "";
  }

  // Validate stored fingerprint (8 hex chars after colon)
  const char* const fpTok = colon + 1;
  if (strlen(fpTok) != 8) {
    LOG_DBG("KODoc", "Hash cache invalidated: bad fingerprint length (%zu)", strlen(fpTok));
    return "";
  }
  for (const char* p = fpTok; *p; ++p) {
    const char c = *p;
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      LOG_DBG("KODoc", "Hash cache invalidated: non-hex character '%c' in fingerprint", c);
      return "";
    }
  }
  if (currentFingerprint != fpTok) {
    LOG_DBG("KODoc", "Hash cache invalidated: fingerprint changed (%s != %s)", fpTok, currentFingerprint.c_str());
    return "";
  }

  // Hash starts after the newline we replaced; trim trailing whitespace
  std::string hash = newline + 1;
  while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r' || hash.back() == ' ')) {
    hash.pop_back();
  }

  // Hash must be exactly 32 hex characters.
  if (hash.size() != 32) {
    LOG_DBG("KODoc", "Hash cache invalidated: wrong hash length (%zu)", hash.size());
    return "";
  }
  for (char c : hash) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      LOG_DBG("KODoc", "Hash cache invalidated: non-hex character '%c' in hash", c);
      return "";
    }
  }

  LOG_DBG("KODoc", "Hash cache hit: %s", hash.c_str());
  return hash;
}

void KOReaderDocumentId::saveCachedHash(const std::string& cacheFilePath, const size_t fileSize,
                                        const std::string& fingerprint, const std::string& hash) {
  // Ensure the book's cache directory exists before writing
  const size_t lastSlash = cacheFilePath.rfind('/');
  if (lastSlash != std::string::npos) {
    Storage.ensureDirectoryExists(cacheFilePath.substr(0, lastSlash).c_str());
  }

  // Format: "<filesize>:<fingerprint>\n<hash>"
  char content[128];
  snprintf(content, sizeof(content), "%zu:%s\n%s", fileSize, fingerprint.c_str(), hash.c_str());
  const String contentStr(content);

  if (!Storage.writeFile(cacheFilePath.c_str(), contentStr)) {
    LOG_DBG("KODoc", "Failed to write hash cache to %s", cacheFilePath.c_str());
  }
}

std::string KOReaderDocumentId::calculateFromFilename(const std::string& filePath) {
  const std::string filename = getFilename(filePath);
  if (filename.empty()) {
    return "";
  }

  MD5Builder md5;
  md5.begin();
  md5.add(filename.c_str());
  md5.calculate();

  std::string result = md5.toString().c_str();
  LOG_DBG("KODoc", "Filename hash: %s (from '%s')", result.c_str(), filename.c_str());
  return result;
}

size_t KOReaderDocumentId::getOffset(int i) {
  // Offset = 1024 << (2*i)
  // For i = -1: KOReader uses a value of 0
  // For i >= 0: 1024 << (2*i)
  if (i < 0) {
    return 0;
  }
  return CHUNK_SIZE << (2 * i);
}

std::string KOReaderDocumentId::calculate(const std::string& filePath) {
  FsFile file;
  if (!Storage.openFileForRead("KODoc", filePath, file)) {
    LOG_DBG("KODoc", "Failed to open file: %s", filePath.c_str());
    return "";
  }

  const size_t fileSize = file.fileSize();

  // Compute a lightweight fingerprint from the file's modification time.
  // The underlying FsFile API provides getModifyDateTime which returns two
  // packed 16-bit values (date and time).  Concatenate these as eight hex
  // digits to produce the token stored in the cache header.
  uint16_t date = 0, time = 0;
  if (!file.getModifyDateTime(&date, &time)) {
    // If timestamp isn't available for some reason, fall back to a sentinel.
    date = 0;
    time = 0;
  }
  char fpBuf[9];
  // two 16-bit numbers => 4 hex digits each
  sprintf(fpBuf, "%04x%04x", date, time);
  const std::string fingerprintTok(fpBuf);

  // Return persisted hash if the file size and fingerprint haven't changed.
  const std::string cacheFilePath = getCacheFilePath(filePath);
  const std::string cached = loadCachedHash(cacheFilePath, fileSize, fingerprintTok);
  if (!cached.empty()) {
    file.close();
    return cached;
  }

  LOG_DBG("KODoc", "Calculating hash for file: %s (size: %zu)", filePath.c_str(), fileSize);

  // Initialize MD5 builder
  MD5Builder md5;
  md5.begin();

  // Buffer for reading chunks
  uint8_t buffer[CHUNK_SIZE];
  size_t totalBytesRead = 0;

  // Read from each offset (i = -1 to 10)
  for (int i = -1; i < OFFSET_COUNT - 1; i++) {
    const size_t offset = getOffset(i);

    // Skip if offset is beyond file size
    if (offset >= fileSize) {
      continue;
    }

    // Seek to offset
    if (!file.seekSet(offset)) {
      LOG_DBG("KODoc", "Failed to seek to offset %zu", offset);
      continue;
    }

    // Read up to CHUNK_SIZE bytes
    const size_t bytesToRead = std::min(CHUNK_SIZE, fileSize - offset);
    const size_t bytesRead = file.read(buffer, bytesToRead);

    if (bytesRead > 0) {
      md5.add(buffer, bytesRead);
      totalBytesRead += bytesRead;
    }
  }

  file.close();

  // Calculate final hash
  md5.calculate();
  std::string result = md5.toString().c_str();

  LOG_DBG("KODoc", "Hash calculated: %s (from %zu bytes)", result.c_str(), totalBytesRead);

  saveCachedHash(cacheFilePath, fileSize, fingerprintTok, result);

  return result;
}
