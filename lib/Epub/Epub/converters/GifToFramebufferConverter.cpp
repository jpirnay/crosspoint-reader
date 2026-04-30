#include "GifToFramebufferConverter.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdFat.h>

#include "GifToBmpConverter.h"
#include "ImageDecoderFactory.h"
#include "PngToFramebufferConverter.h"
#include "JpegToFramebufferConverter.h"
#include "Bitmap.h"

bool GifToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasGifExtension(extension);
}

bool GifToFramebufferConverter::getDimensions(const std::string& imagePath, ImageDimensions& dims) const {
  FsFile file;
  if (!Storage.openFileForRead("GIF", imagePath, file)) {
    LOG_ERR("GIF", "Failed to open GIF for dimensions: %s", imagePath.c_str());
    return false;
  }

  uint8_t header[10];
  if (file.read(header, sizeof(header)) != sizeof(header)) {
    LOG_ERR("GIF", "Short read on GIF header: %s", imagePath.c_str());
    file.close();
    return false;
  }
  file.close();

  if (memcmp(header, "GIF87a", 6) != 0 && memcmp(header, "GIF89a", 6) != 0) {
    LOG_ERR("GIF", "Invalid GIF header: %s", imagePath.c_str());
    return false;
  }

  dims.width = header[6] | (header[7] << 8);
  dims.height = header[8] | (header[9] << 8);
  return dims.width > 0 && dims.height > 0;
}

bool GifToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  LOG_DBG("GIF", "Decoding GIF: %s", imagePath.c_str());

  FsFile gifFile;
  if (!Storage.openFileForRead("GIF", imagePath, gifFile)) {
    LOG_ERR("GIF", "Failed to open GIF file: %s", imagePath.c_str());
    return false;
  }

  const std::string tempBmpPath = imagePath + ".gif.bmp";
  FsFile bmpFile;
  if (!Storage.openFileForWrite("GIF", tempBmpPath, bmpFile)) {
    LOG_ERR("GIF", "Failed to create temp BMP file: %s", tempBmpPath.c_str());
    gifFile.close();
    return false;
  }

  bool success = GifToBmpConverter::gifFileToBmpStream(gifFile, bmpFile, config.maxWidth, config.maxHeight);
  bmpFile.close();
  gifFile.close();

  if (!success) {
    Storage.remove(tempBmpPath.c_str());
    return false;
  }

  FsFile bmpRead;
  if (!Storage.openFileForRead("GIF", tempBmpPath, bmpRead)) {
    LOG_ERR("GIF", "Failed to reopen temp BMP file: %s", tempBmpPath.c_str());
    Storage.remove(tempBmpPath.c_str());
    return false;
  }

  Bitmap bitmap(bmpRead);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    LOG_ERR("GIF", "Failed to parse temp BMP file: %s", tempBmpPath.c_str());
    bmpRead.close();
    Storage.remove(tempBmpPath.c_str());
    return false;
  }

  renderer.drawBitmap(bitmap, config.x, config.y, config.maxWidth, config.maxHeight, 0, 0);
  bmpRead.close();
  Storage.remove(tempBmpPath.c_str());
  return true;
}
