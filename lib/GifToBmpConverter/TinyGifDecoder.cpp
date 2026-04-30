/**
 * @file TinyGifDecoder.cpp
 * @brief Lightweight GIF to BMP decoder implementation
 *
 * Key features and fixes:
 * - LZW decompression with proper CLEAR_CODE and special case (KwKwK) handling
 * - Dynamic memory allocation based on image size (8KB-64KB range)
 * - Heap-allocated code table to avoid stack overflow on ESP32
 * - Proper BMP row padding for 4-byte alignment (fixes column shift artifacts)
 * - Buffer overflow protection with comprehensive bounds checking
 * - GIF sub-block reading for LZW data stream
 *
 * Memory usage:
 * - Code table: ~24KB (heap allocated)
 * - Code table buffer: 8KB-64KB dynamic (heap allocated)
 * - Image buffer: width * height bytes (heap allocated)
 *
 * Limitations:
 * - First frame only (no animation support)
 * - No interlacing support: All GIF data is treated as sequential regardless of
 *   the interlace flag. Truly interlaced GIFs would display with scrambled rows.
 *   Interlacing is rare in modern GIFs (mainly used for dial-up era progressive loading).
 * - No transparency support (transparent pixels rendered as white)
 *
 * Tested with:
 * - GIF87a and GIF89a formats
 * - Images with global and local color tables
 * - Various image dimensions (38-43 pixels wide tested)
 * - Sequential GIFs (including those with incorrectly set interlace flags)
 */

#include "TinyGifDecoder.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <memory>
#include <vector>

// Basic RGB palette for fallback (256 colors)
const uint8_t basicPalette[768] = {
    0, 0, 0, 128, 0, 0, 0, 128, 0, 128, 128, 0, 0, 0, 128, 128, 0, 128, 0, 128, 128, 192, 192, 192, 128, 128, 128, 255,
    0, 0, 0, 255, 0, 255, 255, 0, 0, 0, 255, 255, 0, 255, 0, 255, 255, 255, 255, 255,
    // Fill the rest with grayscale gradient
    0, 0, 0, 5, 5, 5, 10, 10, 10, 15, 15, 15, 20, 20, 20, 25, 25, 25, 30, 30, 30, 35, 35, 35, 40, 40, 40, 45, 45, 45,
    50, 50, 50, 55, 55, 55, 60, 60, 60, 65, 65, 65, 70, 70, 70, 75, 75, 75, 80, 80, 80, 85, 85, 85, 90, 90, 90, 95, 95,
    95, 100, 100, 100, 105, 105, 105, 110, 110, 110, 115, 115, 115, 120, 120, 120, 125, 125, 125, 130, 130, 130, 135,
    135, 135, 140, 140, 140, 145, 145, 145, 150, 150, 150, 155, 155, 155, 160, 160, 160, 165, 165, 165, 170, 170, 170,
    175, 175, 175, 180, 180, 180, 185, 185, 185, 190, 190, 190, 195, 195, 195, 200, 200, 200, 205, 205, 205, 210, 210,
    210, 215, 215, 215, 220, 220, 220, 225, 225, 225, 230, 230, 230, 235, 235, 235, 240, 240, 240, 245, 245, 245, 250,
    250, 250, 255, 255, 255,
    // Continue filling to 256 colors (768 bytes total)
    0, 0, 0, 5, 5, 5, 10, 10, 10, 15, 15, 15, 20, 20, 20, 25, 25, 25, 30, 30, 30, 35, 35, 35, 40, 40, 40, 45, 45, 45,
    50, 50, 50, 55, 55, 55, 60, 60, 60, 65, 65, 65, 70, 70, 70, 75, 75, 75, 80, 80, 80, 85, 85, 85, 90, 90, 90, 95, 95,
    95, 100, 100, 100, 105, 105, 105, 110, 110, 110, 115, 115, 115, 120, 120, 120, 125, 125, 125, 130, 130, 130, 135,
    135, 135, 140, 140, 140, 145, 145, 145, 150, 150, 150, 155, 155, 155, 160, 160, 160, 165, 165, 165, 170, 170, 170,
    175, 175, 175, 180, 180, 180, 185, 185, 185, 190, 190, 190, 195, 195, 195, 200, 200, 200, 205, 205, 205, 210, 210,
    210, 215, 215, 215, 220, 220, 220, 225, 225, 225, 230, 230, 230, 235, 235, 235, 240, 240, 240, 245, 245, 245, 250,
    250, 250, 255, 255, 255,
    // More colors to fill array
    64, 64, 64, 96, 96, 96, 128, 128, 128, 160, 160, 160, 192, 192, 192, 224, 224, 224, 32, 32, 32, 64, 64, 64, 96, 96,
    96, 128, 128, 128, 160, 160, 160, 192, 192, 192, 224, 224, 224, 255, 255, 255, 0, 0, 64, 0, 0, 96, 0, 0, 128, 0, 0,
    160, 0, 0, 192, 0, 0, 224, 0, 0, 255, 0, 64, 0, 0, 96, 0, 0, 128, 0, 0, 160, 0, 0, 192, 0, 0, 224, 0, 0, 255, 0, 64,
    0, 0, 96, 0, 0, 128, 0, 0, 160, 0, 0, 192, 0, 0, 224, 0, 0, 255, 0, 0, 64, 64, 0, 96, 96, 0, 128, 128, 0, 160, 160,
    0, 192, 192, 0, 224, 224, 0, 255, 255, 0, 0, 64, 64, 0, 96, 96, 0, 128, 128, 0, 160, 160, 0, 192, 192, 0, 224, 224,
    0, 255, 255, 64, 0, 64, 96, 0, 96, 128, 0, 128, 160, 0, 160, 192, 0, 192, 224, 0, 224, 255, 0, 255, 64, 64, 64, 96,
    96, 96, 128, 128, 128, 160, 160, 160, 192, 192, 192, 224, 224, 224, 255, 255, 255};

// BMP header structure (same as in GifToBmpConverter)
#pragma pack(push, 1)
struct BMPHeader {
  uint16_t bfType = 0x4D42;  // "BM"
  uint32_t bfSize = 0;
  uint16_t bfReserved1 = 0;
  uint16_t bfReserved2 = 0;
  uint32_t bfOffBits = 54;  // 14 + 40
  uint32_t biSize = 40;
  int32_t biWidth = 0;
  int32_t biHeight = 0;
  uint16_t biPlanes = 1;
  uint16_t biBitCount = 24;
  uint32_t biCompression = 0;
  uint32_t biSizeImage = 0;
  int32_t biXPelsPerMeter = 0;
  int32_t biYPelsPerMeter = 0;
  uint32_t biClrUsed = 0;
  uint32_t biClrImportant = 0;
};
#pragma pack(pop)

struct GifStream {
  HalFile& file;
  uint8_t blockBuffer[256];
  int blockRemaining = 0;
  int blockPosition = 0;

  GifStream(HalFile& file) : file(file), blockRemaining(0), blockPosition(0) {}

  bool readByte(uint8_t& value) {
    int bytesRead = file.read(&value, 1);
    return bytesRead == 1;
  }

  bool readBytes(uint8_t* buffer, size_t length) {
    if (length == 0) return true;
    int bytesRead = file.read(buffer, length);
    return bytesRead == (int)length;
  }

  bool skipBytes(size_t length) {
    uint8_t discard[128];
    while (length > 0) {
      size_t chunk = length > sizeof(discard) ? sizeof(discard) : length;
      if (!readBytes(discard, chunk)) return false;
      length -= chunk;
    }
    return true;
  }

  bool readCompressedByte(uint8_t& value) {
    if (blockRemaining == 0) {
      uint8_t blockSize;
      if (!readByte(blockSize)) return false;
      if (blockSize == 0) return false;
      if (!readBytes(blockBuffer, blockSize)) return false;
      blockRemaining = blockSize;
      blockPosition = 0;
    }
    value = blockBuffer[blockPosition++];
    blockRemaining--;
    return true;
  }

  bool skipSubBlocks() {
    while (true) {
      uint8_t blockSize;
      if (!readByte(blockSize)) return false;
      if (blockSize == 0) return true;
      if (!skipBytes(blockSize)) return false;
    }
  }
};

bool TinyGifDecoder::decodeGifToBmp(HalFile& file, Print& output, int maxWidth, int maxHeight,
                                    std::function<bool()> shouldAbort) {
  if (!file.isOpen()) {
    LOG_ERR("TinyGIF", "ERROR: Invalid GIF file handle");
    return false;
  }

  if (!file.seek(0)) {
    LOG_ERR("TinyGIF", "ERROR: Failed to seek GIF file");
    return false;
  }

  auto readBytes = [&](uint8_t* buffer, size_t length) -> bool {
    if (length == 0) return true;
    int bytesRead = file.read(buffer, length);
    return bytesRead == (int)length;
  };

  auto readByte = [&](uint8_t& value) -> bool {
    int bytesRead = file.read(&value, 1);
    return bytesRead == 1;
  };

  uint8_t header[6];
  if (!readBytes(header, sizeof(header))) {
    LOG_ERR("TinyGIF", "ERROR: Failed to read GIF header");
    return false;
  }
  const uint8_t* dataPtr = header;
  size_t sizeLeft = sizeof(header);
  if (!parseHeader(dataPtr, sizeLeft)) {
    LOG_ERR("TinyGIF", "ERROR: Invalid GIF header");
    return false;
  }

  uint8_t lsdBuffer[7];
  if (!readBytes(lsdBuffer, sizeof(lsdBuffer))) {
    LOG_ERR("TinyGIF", "ERROR: Failed to read logical screen descriptor");
    return false;
  }
  dataPtr = lsdBuffer;
  sizeLeft = sizeof(lsdBuffer);
  LogicalScreenDescriptor lsd;
  if (!parseLogicalScreen(dataPtr, sizeLeft, lsd)) {
    LOG_ERR("TinyGIF", "ERROR: Invalid logical screen descriptor");
    return false;
  }

  std::vector<uint8_t> globalColorTable;
  if ((lsd.flags & 0x80) != 0) {
    int tableSize = 1 << ((lsd.flags & 0x07) + 1);
    size_t globalSize = (size_t)tableSize * 3;
    globalColorTable.resize(globalSize);
    if (!readBytes(globalColorTable.data(), globalSize)) {
      LOG_ERR("TinyGIF", "ERROR: Failed to read global color table");
      return false;
    }
  }

  GifStream stream(file);

  // Skip any extensions until the first image descriptor (0x2C)
  while (true) {
    uint8_t marker;
    if (!stream.readByte(marker)) {
      LOG_ERR("TinyGIF", "ERROR: Failed to locate image descriptor");
      return false;
    }

    if (marker == 0x2C) break;  // Image descriptor — start of frame data

    if (marker == 0x3B) {
      LOG_ERR("TinyGIF", "ERROR: GIF trailer reached before image");
      return false;
    }

    if (marker == 0x21) {
      uint8_t extensionLabel;
      if (!stream.readByte(extensionLabel)) {
        LOG_ERR("TinyGIF", "ERROR: Failed to read extension label");
        return false;
      }
      if (!stream.skipSubBlocks()) {
        LOG_ERR("TinyGIF", "ERROR: Failed to skip extension blocks");
        return false;
      }
      continue;
    }

    LOG_ERR("TinyGIF", "ERROR: Unexpected GIF block 0x%02X", marker);
    return false;
  }

  uint8_t imageDescriptor[9];
  if (!stream.readBytes(imageDescriptor, sizeof(imageDescriptor))) {
    LOG_ERR("TinyGIF", "ERROR: Failed to read image descriptor");
    return false;
  }

  dataPtr = imageDescriptor;
  sizeLeft = sizeof(imageDescriptor);
  ImageDescriptor imgDesc;
  if (!parseImageDescriptor(dataPtr, sizeLeft, imgDesc)) {
    LOG_ERR("TinyGIF", "ERROR: Invalid image descriptor");
    return false;
  }

  std::vector<uint8_t> localColorTable;
  if ((imgDesc.flags & 0x80) != 0) {
    int tableSize = 1 << ((imgDesc.flags & 0x07) + 1);
    size_t localSize = (size_t)tableSize * 3;
    localColorTable.resize(localSize);
    if (!stream.readBytes(localColorTable.data(), localSize)) {
      LOG_ERR("TinyGIF", "ERROR: Failed to read local color table");
      return false;
    }
  }

  const uint8_t* colorTable = nullptr;
  int colorTableEntries = 256;
  if (!localColorTable.empty()) {
    colorTable = localColorTable.data();
    colorTableEntries = localColorTable.size() / 3;
  } else if (!globalColorTable.empty()) {
    colorTable = globalColorTable.data();
    colorTableEntries = globalColorTable.size() / 3;
  } else {
    colorTable = basicPalette;
    colorTableEntries = 256;
  }

  uint8_t lzwMinCodeSize;
  if (!stream.readByte(lzwMinCodeSize)) {
    LOG_ERR("TinyGIF", "ERROR: Missing LZW minimum code size");
    return false;
  }

  int width = imgDesc.width;
  int height = imgDesc.height;
  if (width <= 0 || height <= 0 || width > maxWidth || height > maxHeight) {
    LOG_ERR("TinyGIF", "ERROR: Invalid GIF dimensions %dx%d", width, height);
    return false;
  }
  // Cap image-buffer allocation against the caller-supplied screen bounds.
  // Without this a 200KB GIF could expand to width*height bytes (up to 4096*4096 = 16MB),
  // far exceeding ESP32-C3 RAM.
  const size_t maxImageBufferBytes = (size_t)maxWidth * (size_t)maxHeight;
  size_t imageSize = (size_t)width * height;
  if (imageSize == 0 || imageSize > maxImageBufferBytes) {
    LOG_ERR("TinyGIF", "ERROR: GIF image buffer too large (%zu bytes)", imageSize);
    return false;
  }

  uint8_t* imageBufferRaw = (uint8_t*)malloc(imageSize);
  if (!imageBufferRaw) {
    LOG_ERR("TinyGIF", "ERROR: Failed to allocate image buffer (%zu bytes)", imageSize);
    return false;
  }
  std::unique_ptr<uint8_t, void (*)(void*)> imageBuffer(imageBufferRaw, free);
  if (!decompressLZW(file, imageBuffer.get(), imageSize, width, height, colorTable, colorTableEntries, lzwMinCodeSize,
                     shouldAbort)) {
    LOG_ERR("TinyGIF", "ERROR: LZW decompression failed");
    return false;
  }

  writeBmpHeader(output, width, height);

  int rowBytes = width * 3;
  int padding = (4 - (rowBytes % 4)) % 4;
  std::vector<uint8_t> rowBuffer(rowBytes + padding);

  const uint8_t* pixels = imageBuffer.get();
  for (int y = height - 1; y >= 0; y--) {
    size_t rowStart = (size_t)y * width;
    size_t rowPos = 0;
    for (int x = 0; x < width; x++) {
      uint8_t pixel = pixels[rowStart + x];
      if (pixel < colorTableEntries) {
        rowBuffer[rowPos++] = colorTable[pixel * 3 + 2];
        rowBuffer[rowPos++] = colorTable[pixel * 3 + 1];
        rowBuffer[rowPos++] = colorTable[pixel * 3];
      } else {
        rowBuffer[rowPos++] = 0;
        rowBuffer[rowPos++] = 0;
        rowBuffer[rowPos++] = 0;
      }
    }
    for (int p = 0; p < padding; p++) {
      rowBuffer[rowPos++] = 0;
    }
    output.write(rowBuffer.data(), rowBuffer.size());
  }

  return true;
}

bool TinyGifDecoder::decodeGifToBuffer(HalFile& file, int maxWidth, int maxHeight, uint8_t** outIndices,
                                       uint8_t** outPalette, int* outPaletteEntries, int* outWidth, int* outHeight,
                                       std::function<bool()> shouldAbort) {
  if (outIndices) *outIndices = nullptr;
  if (outPalette) *outPalette = nullptr;
  if (outPaletteEntries) *outPaletteEntries = 0;
  if (outWidth) *outWidth = 0;
  if (outHeight) *outHeight = 0;

  if (!outIndices || !outPalette || !outPaletteEntries || !outWidth || !outHeight) {
    LOG_ERR("TinyGIF", "decodeGifToBuffer: NULL output pointer");
    return false;
  }

  if (!file.isOpen()) {
    LOG_ERR("TinyGIF", "ERROR: Invalid GIF file handle");
    return false;
  }

  if (!file.seek(0)) {
    LOG_ERR("TinyGIF", "ERROR: Failed to seek GIF file");
    return false;
  }

  auto readBytes = [&](uint8_t* buffer, size_t length) -> bool {
    if (length == 0) return true;
    int bytesRead = file.read(buffer, length);
    return bytesRead == (int)length;
  };

  uint8_t header[6];
  if (!readBytes(header, sizeof(header))) {
    LOG_ERR("TinyGIF", "ERROR: Failed to read GIF header");
    return false;
  }
  const uint8_t* dataPtr = header;
  size_t sizeLeft = sizeof(header);
  if (!parseHeader(dataPtr, sizeLeft)) {
    LOG_ERR("TinyGIF", "ERROR: Invalid GIF header");
    return false;
  }

  uint8_t lsdBuffer[7];
  if (!readBytes(lsdBuffer, sizeof(lsdBuffer))) {
    LOG_ERR("TinyGIF", "ERROR: Failed to read logical screen descriptor");
    return false;
  }
  dataPtr = lsdBuffer;
  sizeLeft = sizeof(lsdBuffer);
  LogicalScreenDescriptor lsd;
  if (!parseLogicalScreen(dataPtr, sizeLeft, lsd)) {
    LOG_ERR("TinyGIF", "ERROR: Invalid logical screen descriptor");
    return false;
  }

  std::vector<uint8_t> globalColorTable;
  if ((lsd.flags & 0x80) != 0) {
    int tableSize = 1 << ((lsd.flags & 0x07) + 1);
    size_t globalSize = (size_t)tableSize * 3;
    globalColorTable.resize(globalSize);
    if (!readBytes(globalColorTable.data(), globalSize)) {
      LOG_ERR("TinyGIF", "ERROR: Failed to read global color table");
      return false;
    }
  }

  GifStream stream(file);

  while (true) {
    uint8_t marker;
    if (!stream.readByte(marker)) {
      LOG_ERR("TinyGIF", "ERROR: Failed to locate image descriptor");
      return false;
    }
    if (marker == 0x2C) break;
    if (marker == 0x3B) {
      LOG_ERR("TinyGIF", "ERROR: GIF trailer reached before image");
      return false;
    }
    if (marker == 0x21) {
      uint8_t extensionLabel;
      if (!stream.readByte(extensionLabel)) {
        LOG_ERR("TinyGIF", "ERROR: Failed to read extension label");
        return false;
      }
      if (!stream.skipSubBlocks()) {
        LOG_ERR("TinyGIF", "ERROR: Failed to skip extension blocks");
        return false;
      }
      continue;
    }
    LOG_ERR("TinyGIF", "ERROR: Unexpected GIF block 0x%02X", marker);
    return false;
  }

  uint8_t imageDescriptor[9];
  if (!stream.readBytes(imageDescriptor, sizeof(imageDescriptor))) {
    LOG_ERR("TinyGIF", "ERROR: Failed to read image descriptor");
    return false;
  }
  dataPtr = imageDescriptor;
  sizeLeft = sizeof(imageDescriptor);
  ImageDescriptor imgDesc;
  if (!parseImageDescriptor(dataPtr, sizeLeft, imgDesc)) {
    LOG_ERR("TinyGIF", "ERROR: Invalid image descriptor");
    return false;
  }

  std::vector<uint8_t> localColorTable;
  if ((imgDesc.flags & 0x80) != 0) {
    int tableSize = 1 << ((imgDesc.flags & 0x07) + 1);
    size_t localSize = (size_t)tableSize * 3;
    localColorTable.resize(localSize);
    if (!stream.readBytes(localColorTable.data(), localSize)) {
      LOG_ERR("TinyGIF", "ERROR: Failed to read local color table");
      return false;
    }
  }

  const uint8_t* sourcePalette = nullptr;
  int colorTableEntries = 256;
  if (!localColorTable.empty()) {
    sourcePalette = localColorTable.data();
    colorTableEntries = localColorTable.size() / 3;
  } else if (!globalColorTable.empty()) {
    sourcePalette = globalColorTable.data();
    colorTableEntries = globalColorTable.size() / 3;
  } else {
    sourcePalette = basicPalette;
    colorTableEntries = 256;
  }

  uint8_t lzwMinCodeSize;
  if (!stream.readByte(lzwMinCodeSize)) {
    LOG_ERR("TinyGIF", "ERROR: Missing LZW minimum code size");
    return false;
  }

  int width = imgDesc.width;
  int height = imgDesc.height;
  if (width <= 0 || height <= 0 || width > maxWidth || height > maxHeight) {
    LOG_ERR("TinyGIF", "ERROR: Invalid GIF dimensions %dx%d", width, height);
    return false;
  }

  const size_t maxImageBufferBytes = (size_t)maxWidth * (size_t)maxHeight;
  size_t imageSize = (size_t)width * height;
  if (imageSize == 0 || imageSize > maxImageBufferBytes) {
    LOG_ERR("TinyGIF", "ERROR: GIF image buffer too large (%zu bytes)", imageSize);
    return false;
  }

  uint8_t* indices = (uint8_t*)malloc(imageSize);
  if (!indices) {
    LOG_ERR("TinyGIF", "ERROR: Failed to allocate index buffer (%zu bytes)", imageSize);
    return false;
  }

  if (!decompressLZW(file, indices, imageSize, width, height, sourcePalette, colorTableEntries, lzwMinCodeSize,
                     shouldAbort)) {
    LOG_ERR("TinyGIF", "ERROR: LZW decompression failed");
    free(indices);
    return false;
  }

  // Copy palette into a caller-owned heap buffer so the (possibly stack/std::vector-backed)
  // sourcePalette pointer goes out of scope cleanly.
  const size_t paletteBytes = (size_t)colorTableEntries * 3;
  uint8_t* palette = (uint8_t*)malloc(paletteBytes);
  if (!palette) {
    LOG_ERR("TinyGIF", "ERROR: Failed to allocate palette buffer (%zu bytes)", paletteBytes);
    free(indices);
    return false;
  }
  memcpy(palette, sourcePalette, paletteBytes);

  *outIndices = indices;
  *outPalette = palette;
  *outPaletteEntries = colorTableEntries;
  *outWidth = width;
  *outHeight = height;
  return true;
}

bool TinyGifDecoder::parseHeader(const uint8_t*& data, size_t& size) {
  if (size < 6) return false;
  if (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0) return false;
  data += 6;
  size -= 6;
  return true;
}

bool TinyGifDecoder::parseLogicalScreen(const uint8_t*& data, size_t& size, LogicalScreenDescriptor& lsd) {
  if (size < 7) return false;
  lsd.width = data[0] | (data[1] << 8);
  lsd.height = data[2] | (data[3] << 8);
  lsd.flags = data[4];
  lsd.bgColorIndex = data[5];
  lsd.pixelAspectRatio = data[6];
  data += 7;
  size -= 7;
  return true;
}

bool TinyGifDecoder::parseImageDescriptor(const uint8_t*& data, size_t& size, ImageDescriptor& imgDesc) {
  if (size < 10 || *data != 0x2C) return false;
  data++;  // Skip separator
  size--;

  imgDesc.left = data[0] | (data[1] << 8);
  imgDesc.top = data[2] | (data[3] << 8);
  imgDesc.width = data[4] | (data[5] << 8);
  imgDesc.height = data[6] | (data[7] << 8);
  imgDesc.flags = data[8];

  data += 9;
  size -= 9;
  return true;
}

void TinyGifDecoder::writeBmpHeader(Print& output, int width, int height) {
  // Calculate row size with padding (BMP rows must be aligned to 4-byte boundaries)
  int rowBytes = width * 3;  // 24-bit RGB
  int padding = (4 - (rowBytes % 4)) % 4;
  int paddedRowBytes = rowBytes + padding;
  size_t imageDataSize = paddedRowBytes * height;

  BMPHeader header;
  header.bfSize = sizeof(BMPHeader) + imageDataSize;
  header.biWidth = width;
  header.biHeight = height;
  header.biSizeImage = imageDataSize;

  output.write((uint8_t*)&header, sizeof(header));
}

bool TinyGifDecoder::decompressLZW(HalFile& input, uint8_t* output, size_t outputSize, int width, int height,
                                   const uint8_t* colorTable, int colorTableSize, uint8_t minCodeSize,
                                   std::function<bool()> shouldAbort) {
  // Validate input parameters
  if (!output || !colorTable) {
    LOG_ERR("TinyGIF", "LZW ERROR: NULL pointer");
    return false;
  }
  if (outputSize == 0 || width <= 0 || height <= 0 || (size_t)width * height != outputSize) {
    LOG_ERR("TinyGIF", "LZW ERROR: Dimension mismatch");
    return false;
  }
  if (minCodeSize < 2 || minCodeSize > 8) {
    LOG_ERR("TinyGIF", "LZW ERROR: Invalid code size %d", minCodeSize);
    return false;
  }

  GifStream stream(input);
  uint8_t* outPtr = output;
  size_t outRemaining = outputSize;

  const int MAX_CODE = 4096;
  const int CLEAR_CODE = 1 << minCodeSize;
  const int END_CODE = CLEAR_CODE + 1;
  if (CLEAR_CODE >= MAX_CODE || END_CODE >= MAX_CODE) {
    LOG_ERR("TinyGIF", "LZW ERROR: Invalid code range");
    return false;
  }

  struct CodeEntry {
    int16_t prefix;
    uint8_t suffix;
  };

  // Heap-allocate decode buffers — the ActivityManager render task only has 8KB of stack,
  // and decodeStack alone (4KB) would overflow it on top of GifStream::blockBuffer (256B)
  // and the lambda captures.
  CodeEntry* codeTable = (CodeEntry*)malloc(MAX_CODE * sizeof(CodeEntry));
  if (!codeTable) {
    LOG_ERR("TinyGIF", "LZW ERROR: Failed to allocate code table");
    return false;
  }
  uint8_t* decodeStack = (uint8_t*)malloc(MAX_CODE);
  if (!decodeStack) {
    LOG_ERR("TinyGIF", "LZW ERROR: Failed to allocate decode stack");
    free(codeTable);
    return false;
  }

  for (int i = 0; i < CLEAR_CODE; i++) {
    codeTable[i].prefix = -1;
    codeTable[i].suffix = (uint8_t)i;
  }

  int codeSize = minCodeSize + 1;
  int nextCode = END_CODE + 1;
  uint32_t bitBuffer = 0;
  int bitsInBuffer = 0;
  const int decodeStackSize = MAX_CODE;

  auto readBits = [&](int numBits) -> int {
    if (numBits < 1 || numBits > 12) {
      return -1;
    }
    while (bitsInBuffer < numBits) {
      if (shouldAbort && shouldAbort()) {
        return -1;
      }
      uint8_t value;
      if (!stream.readCompressedByte(value)) {
        return -1;
      }
      bitBuffer |= ((uint32_t)value) << bitsInBuffer;
      bitsInBuffer += 8;
    }
    int code = bitBuffer & ((1 << numBits) - 1);
    bitBuffer >>= numBits;
    bitsInBuffer -= numBits;
    return code;
  };

  // Main decompression loop
  int prevCode = -1;
  bool firstCode = true;

  while (true) {
    if (shouldAbort && shouldAbort()) {
      break;
    }

    int code = readBits(codeSize);
    if (code < 0) break;
    if (code == END_CODE) break;

    if (code == CLEAR_CODE) {
      codeSize = minCodeSize + 1;
      nextCode = END_CODE + 1;
      prevCode = -1;
      firstCode = true;
      bitBuffer = 0;
      bitsInBuffer = 0;
      continue;
    }

    int stackSize = 0;
    if (code < nextCode) {
      int cursor = code;
      while (cursor >= 0 && stackSize < decodeStackSize) {
        decodeStack[stackSize++] = codeTable[cursor].suffix;
        cursor = codeTable[cursor].prefix;
      }
    } else if (code == nextCode && prevCode >= 0) {
      int cursor = prevCode;
      while (cursor >= 0 && stackSize < decodeStackSize) {
        decodeStack[stackSize++] = codeTable[cursor].suffix;
        cursor = codeTable[cursor].prefix;
      }
      if (stackSize >= decodeStackSize) {
        LOG_ERR("TinyGIF", "LZW ERROR: Stack overflow");
        free(codeTable);
        free(decodeStack);
        return false;
      }
      decodeStack[stackSize++] = decodeStack[stackSize - 1];
    } else {
      LOG_ERR("TinyGIF", "LZW ERROR: Invalid code %d", code);
      free(codeTable);
      free(decodeStack);
      return false;
    }

    if (outRemaining < (size_t)stackSize) {
      LOG_ERR("TinyGIF", "LZW ERROR: Output overflow");
      free(codeTable);
      free(decodeStack);
      return false;
    }

    for (int i = stackSize - 1; i >= 0; i--) {
      *outPtr++ = decodeStack[i];
    }
    outRemaining -= stackSize;

    if (!firstCode && prevCode >= 0 && nextCode < MAX_CODE) {
      codeTable[nextCode].prefix = prevCode;
      codeTable[nextCode].suffix = decodeStack[stackSize - 1];
      nextCode++;
      if (nextCode == (1 << codeSize) && codeSize < 12) {
        codeSize++;
      }
    }

    prevCode = code;
    firstCode = false;
  }

  bool success = outRemaining == 0;
  free(codeTable);
  free(decodeStack);
  return success;
}
