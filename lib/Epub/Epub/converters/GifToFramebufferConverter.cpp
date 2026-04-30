#include "GifToFramebufferConverter.h"

#include <BitmapHelpers.h>
#include <Esp.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <TinyGifDecoder.h>

#include <cstdlib>
#include <cstring>
#include <memory>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "PixelCache.h"

namespace {

// Same heap headroom check as the BMP path — see GifToBmpConverter.cpp for rationale.
constexpr size_t MIN_FREE_HEAP_FOR_GIF = 32 * 1024;

// Mirror PNG's cache size cap. Skipping the cache for huge images keeps us from
// fighting the framebuffer/decoder for the largest free block.
constexpr size_t GIF_MAX_CACHE_BYTES = 48000;

// Build a 256-entry RGB→gray lookup table for the palette. Pre-computing this once
// turns each pixel lookup into a single byte fetch instead of three multiplies +
// shift, which matters when scaling the source up to a larger destination.
// Palette indices outside the table size are flagged as 0xFF (treated as black/skip).
void buildGrayLut(const uint8_t* palette, int paletteEntries, uint8_t* grayLut) {
  for (int i = 0; i < 256; i++) {
    if (i < paletteEntries) {
      const uint8_t* p = &palette[i * 3];
      // ITU-R BT.601 luma weights (matches PngToFramebufferConverter::convertLineToGray).
      grayLut[i] = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
    } else {
      grayLut[i] = 0;
    }
  }
}

uint8_t ditherPixel(uint8_t gray, int dstX, int outX, int outY, const RenderConfig& config,
                    Atkinson1BitDitherer* atkinson1Bit) {
  if (atkinson1Bit) {
    return atkinson1Bit->processPixel(gray, dstX) ? 3 : 0;
  }
  if (!config.useDithering) {
    return quantizeGray4Level(gray);
  }
  // We only support Bayer here; Atkinson/DiffusedBayer require carrying error-state
  // across pixel writes which complicates the all-at-once decode model. Bayer is
  // ordered and stateless, so we can apply it on the fly without per-pixel state.
  return applyBayerDither4Level(gray, outX, outY);
}

}  // namespace

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

  const int width = header[6] | (header[7] << 8);
  const int height = header[8] | (header[9] << 8);
  if (width <= 0 || height <= 0 || width > 0x7FFF || height > 0x7FFF) {
    LOG_ERR("GIF", "Implausible GIF dimensions %dx%d: %s", width, height, imagePath.c_str());
    return false;
  }

  dims.width = static_cast<int16_t>(width);
  dims.height = static_cast<int16_t>(height);
  return true;
}

bool GifToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  LOG_DBG("GIF", "Decoding GIF: %s", imagePath.c_str());

  const size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_GIF) {
    LOG_ERR("GIF", "Not enough heap for GIF decoder (%u free, need %u)", freeHeap,
            static_cast<unsigned>(MIN_FREE_HEAP_FOR_GIF));
    return false;
  }

  FsFile gifFile;
  if (!Storage.openFileForRead("GIF", imagePath, gifFile)) {
    LOG_ERR("GIF", "Failed to open GIF file: %s", imagePath.c_str());
    return false;
  }

  // Decode straight to palette indices + RGB palette. The TinyGifDecoder caps the
  // index buffer at maxWidth*maxHeight so we won't blow the heap on a malformed GIF.
  uint8_t* indicesRaw = nullptr;
  uint8_t* paletteRaw = nullptr;
  int paletteEntries = 0;
  int srcWidth = 0;
  int srcHeight = 0;
  const bool decodeOk =
      TinyGifDecoder::decodeGifToBuffer(gifFile, renderer.getScreenWidth(), renderer.getScreenHeight(), &indicesRaw,
                                        &paletteRaw, &paletteEntries, &srcWidth, &srcHeight, nullptr);
  gifFile.close();

  if (!decodeOk) {
    LOG_ERR("GIF", "Failed to decode GIF: %s", imagePath.c_str());
    return false;
  }

  std::unique_ptr<uint8_t, void (*)(void*)> indices(indicesRaw, free);
  std::unique_ptr<uint8_t, void (*)(void*)> palette(paletteRaw, free);

  if (!validateImageDimensions(srcWidth, srcHeight, "GIF")) {
    return false;
  }

  // Compute destination size. The parser passes useExactDimensions=true with pre-
  // calculated maxWidth/maxHeight so the rendered image lines up with the chapter
  // layout boxes; otherwise fit within the bounds without upscaling (matches PNG).
  int dstWidth, dstHeight;
  if (config.useExactDimensions && config.maxWidth > 0 && config.maxHeight > 0) {
    dstWidth = config.maxWidth;
    dstHeight = config.maxHeight;
  } else {
    const float scaleX = (config.maxWidth > 0) ? (float)config.maxWidth / srcWidth : 1.0f;
    const float scaleY = (config.maxHeight > 0) ? (float)config.maxHeight / srcHeight : 1.0f;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;
    if (scale > 1.0f) scale = 1.0f;  // never upscale
    dstWidth = (int)(srcWidth * scale);
    dstHeight = (int)(srcHeight * scale);
    if (dstWidth < 1) dstWidth = 1;
    if (dstHeight < 1) dstHeight = 1;
  }

  // Optional render cache (.pxc): writes a 2-bit-per-pixel buffer the next render
  // can replay without re-decoding the GIF.
  PixelCache cache;
  bool caching = !config.cachePath.empty();
  if (caching) {
    const size_t cacheSize = (size_t)((dstWidth + 3) / 4) * dstHeight;
    if (cacheSize > GIF_MAX_CACHE_BYTES) {
      LOG_DBG("GIF", "Skipping cache: %zu bytes exceeds GIF limit (%zu)", cacheSize, GIF_MAX_CACHE_BYTES);
      caching = false;
    } else if (!cache.allocate(dstWidth, dstHeight, config.x, config.y)) {
      LOG_ERR("GIF", "Failed to allocate cache buffer, continuing without caching");
      caching = false;
    }
  }

  // 1-bit Atkinson dither for monochrome output. Carries error state across pixels
  // and rows, so we walk the destination strictly top-to-bottom, left-to-right.
  std::unique_ptr<Atkinson1BitDitherer> atkinson1Bit;
  if (config.monochromeOutput) {
    atkinson1Bit.reset(new (std::nothrow) Atkinson1BitDitherer(dstWidth));
    if (!atkinson1Bit) {
      LOG_ERR("GIF", "Failed to allocate 1-bit Atkinson ditherer, falling back to Bayer");
    }
  }

  uint8_t grayLut[256];
  buildGrayLut(palette.get(), paletteEntries, grayLut);

  DirectPixelWriter pw;
  pw.init(renderer);
  DirectCacheWriter cw;
  if (caching) cw.init(cache.buffer, cache.bytesPerRow, cache.originX);

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int outXBase = config.x;
  const int outYBase = config.y;
  const uint8_t* srcIndices = indices.get();

  // Bresenham-style integer stepping for nearest-neighbor scaling.
  // ySrcAcc tracks the source-Y position in fixed-point fractions of dstHeight.
  // Same idea as PngToFramebufferConverter::pngDrawCallback's per-row loop, but
  // we own the outer Y loop here since the whole image is already in memory.
  unsigned long renderStart = millis();
  int ySrc = 0;
  int yError = 0;
  for (int dstY = 0; dstY < dstHeight; dstY++) {
    const int outY = outYBase + dstY;
    const bool yVisible = (outY >= 0 && outY < screenHeight);

    if (yVisible) {
      pw.beginRow(outY);
      if (caching) cw.beginRow(outY, outYBase);
    }

    const uint8_t* srcRow = &srcIndices[(size_t)ySrc * srcWidth];

    int xSrc = 0;
    int xError = 0;
    for (int dstX = 0; dstX < dstWidth; dstX++) {
      const uint8_t paletteIndex = srcRow[xSrc];
      const uint8_t gray = grayLut[paletteIndex];
      const int outX = outXBase + dstX;

      // Atkinson is stateful — call it for every dst pixel so the error
      // distributes correctly even when the pixel falls off-screen.
      const uint8_t pixelValue = ditherPixel(gray, dstX, outX, outY, config, atkinson1Bit.get());

      if (yVisible && outX >= 0 && outX < screenWidth) {
        pw.writePixel(outX, pixelValue);
        if (caching) cw.writePixel(outX, pixelValue);
      }

      xError += srcWidth;
      while (xError >= dstWidth) {
        xError -= dstWidth;
        xSrc++;
      }
    }

    if (atkinson1Bit) atkinson1Bit->nextRow();

    yError += srcHeight;
    while (yError >= dstHeight) {
      yError -= dstHeight;
      ySrc++;
    }
  }
  const unsigned long renderTime = millis() - renderStart;
  LOG_DBG("GIF", "GIF %dx%d -> %dx%d render time: %lums", srcWidth, srcHeight, dstWidth, dstHeight, renderTime);

  if (caching) {
    cache.writeToFile(config.cachePath);
  }

  return true;
}
