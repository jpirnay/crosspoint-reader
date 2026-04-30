#include "GifToBmpConverter.h"

#include <Logging.h>
#include <SdFat.h>

#include "TinyGifDecoder.h"

bool GifToBmpConverter::gifFileToBmpStream(FsFile& input, Print& output, int maxWidth, int maxHeight) {
  return gifFileToBmpStreamWithSize(input, output, maxWidth, maxHeight, nullptr);
}

bool GifToBmpConverter::gifFileToBmpStreamWithSize(FsFile& input, Print& output, int maxWidth, int maxHeight,
                                                   std::function<bool()> shouldAbort) {
  // Use TinyGifDecoder for static GIF images.
  // Streaming from FsFile avoids loading the full GIF into RAM.
  if (input.size() > 200 * 1024) {
    LOG_ERR("GIF", "ERROR: File too large (%zu bytes)", input.size());
    return false;
  }

  return TinyGifDecoder::decodeGifToBmp(input, output, maxWidth, maxHeight, shouldAbort);
}

bool GifToBmpConverter::gifFileToBmpStreamQuick(FsFile& input, Print& output, int maxWidth, int maxHeight) {
  // Quick mode is same as normal for GIF
  return gifFileToBmpStreamWithSize(input, output, maxWidth, maxHeight, nullptr);
}