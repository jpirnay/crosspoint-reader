#include "GifToBmpConverter.h"

#include <Esp.h>
#include <HalStorage.h>
#include <Logging.h>

#include "TinyGifDecoder.h"

namespace {
// Peak heap usage during decode: 12KB code table + 4KB decode stack +
// up to 480*800 (~375KB max-bounded) image buffer + small row buffer + color tables.
// For a typical content image (e.g. 200x300) this is ~75KB. Require enough headroom
// so we fail cleanly rather than partway through.
constexpr size_t MIN_FREE_HEAP_FOR_GIF = 32 * 1024;
}  // namespace

bool GifToBmpConverter::gifFileToBmpStream(HalFile& input, Print& output, int maxWidth, int maxHeight) {
  return gifFileToBmpStreamWithSize(input, output, maxWidth, maxHeight, nullptr);
}

bool GifToBmpConverter::gifFileToBmpStreamWithSize(HalFile& input, Print& output, int maxWidth, int maxHeight,
                                                   std::function<bool()> shouldAbort) {
  // Use TinyGifDecoder for static GIF images.
  // Streaming from FsFile avoids loading the full GIF into RAM.
  if (input.size() > 200 * 1024) {
    LOG_ERR("GIF", "ERROR: File too large (%zu bytes)", input.size());
    return false;
  }

  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_GIF) {
    LOG_ERR("GIF", "ERROR: Not enough heap (%u free, need %u)", freeHeap, static_cast<unsigned>(MIN_FREE_HEAP_FOR_GIF));
    return false;
  }

  return TinyGifDecoder::decodeGifToBmp(input, output, maxWidth, maxHeight, shouldAbort);
}

bool GifToBmpConverter::gifFileToBmpStreamQuick(HalFile& input, Print& output, int maxWidth, int maxHeight) {
  // Quick mode is same as normal for GIF
  return gifFileToBmpStreamWithSize(input, output, maxWidth, maxHeight, nullptr);
}