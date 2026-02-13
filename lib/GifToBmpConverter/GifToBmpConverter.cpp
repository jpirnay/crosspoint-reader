#include "GifToBmpConverter.h"

#include <Logging.h>
#include <SdFat.h>

#include "TinyGifDecoder.h"

bool GifToBmpConverter::gifFileToBmpStream(FsFile& input, Print& output, int maxWidth, int maxHeight) {
  return gifFileToBmpStreamWithSize(input, output, maxWidth, maxHeight, nullptr);
}

bool GifToBmpConverter::gifFileToBmpStreamWithSize(FsFile& input, Print& output, int maxWidth, int maxHeight,
                                                   std::function<bool()> shouldAbort) {
  // Use TinyGifDecoder for static GIF images
  size_t fileSize = input.size();
  if (fileSize > 200 * 1024) {
    LOG_ERR("GIF", "ERROR: File too large (%zu bytes)", fileSize);
    return false;
  }

  uint8_t* fileBuffer = (uint8_t*)malloc(fileSize);
  if (!fileBuffer) {
    LOG_ERR("GIF", "ERROR: Failed to allocate file buffer");
    return false;
  }

  size_t bytesRead = input.read(fileBuffer, fileSize);
  if (bytesRead != fileSize) {
    LOG_ERR("GIF", "ERROR: Read failed (%zu/%zu bytes)", bytesRead, fileSize);
    free(fileBuffer);
    return false;
  }

  bool result = TinyGifDecoder::decodeGifToBmp(fileBuffer, fileSize, output, maxWidth, maxHeight, shouldAbort);
  free(fileBuffer);

  return result;
}

bool GifToBmpConverter::gifFileToBmpStreamQuick(FsFile& input, Print& output, int maxWidth, int maxHeight) {
  // Quick mode is same as normal for GIF
  return gifFileToBmpStreamWithSize(input, output, maxWidth, maxHeight, nullptr);
}