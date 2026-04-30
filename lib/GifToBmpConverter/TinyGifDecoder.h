#pragma once

#include <HalStorage.h>
#include <Print.h>

#include <functional>

/**
 * @brief Lightweight GIF decoder for extracting the first frame
 *
 * TinyGifDecoder is a minimal GIF decoder designed for resource-constrained
 * embedded systems. It extracts only the first frame of a GIF file and converts
 * it to 24-bit BMP format.
 *
 * Features:
 * - LZW decompression with dynamic memory allocation
 * - Supports GIF87a and GIF89a formats
 * - Handles global and local color tables
 * - Proper BMP row padding (4-byte alignment)
 * - Memory-efficient (scales buffer size based on image dimensions)
 *
 * Limitations:
 * - First frame only (no animation support)
 * - No transparency support (transparent pixels rendered as white)
 * - No interlacing support: All GIF data is read sequentially regardless of the
 *   interlace flag. If a GIF file contains truly interlaced data (4-pass format),
 *   the output will have scrambled rows. Modern GIFs rarely use interlacing as it
 *   was mainly for progressive loading over dial-up connections.
 * - Maximum image size: 4096x4096 pixels
 * - Maximum file size: ~200KB
 */
class TinyGifDecoder {
 public:
  /**
   * @brief Decode a GIF file to 24-bit BMP format
   *
   * Decodes the first frame of a GIF file and writes it as a 24-bit BMP
   * to the provided Print stream. The BMP includes proper row padding for
   * 4-byte alignment.
   *
   * @param input Open GIF file handle to read from
   * @param output Print stream to write BMP data to
   * @param maxWidth Maximum allowed image width (default 480)
   * @param maxHeight Maximum allowed image height (default 800)
   * @param shouldAbort Optional callback to check if decoding should abort
   * @return true if decoding succeeds, false on error
   *
   * @note The output BMP is written in bottom-up format (standard BMP)
   * @note Row padding is automatically added for proper BMP alignment
   * @note GIF interlace flag is ignored - all data read sequentially
   * @note Only ERROR messages are printed to Serial on failure
   */
  static bool decodeGifToBmp(HalFile& input, Print& output, int maxWidth = 480, int maxHeight = 800,
                             std::function<bool()> shouldAbort = nullptr);

  /**
   * @brief Decode a GIF file to palette indices + RGB palette (no BMP wrapping)
   *
   * Decodes the first frame of a GIF file directly into caller-provided heap buffers.
   * Used by the framebuffer renderer to apply dithering and write the cache without
   * the temp-BMP indirection.
   *
   * @param input Open GIF file handle to read from
   * @param maxWidth Maximum allowed image width
   * @param maxHeight Maximum allowed image height
   * @param[out] outIndices Palette indices for each pixel (caller frees with free()).
   *                        Size = outWidth * outHeight bytes.
   * @param[out] outPalette RGB palette, 3 bytes per entry (caller frees with free()).
   *                        Size = outPaletteEntries * 3 bytes.
   * @param[out] outPaletteEntries Number of palette entries (max 256)
   * @param[out] outWidth Decoded image width
   * @param[out] outHeight Decoded image height
   * @param shouldAbort Optional callback to abort decoding
   * @return true on success; on failure all out buffers are nullptr/0
   *
   * @note The palette is RGB (R, G, B) order, not BGR.
   * @note Palette entries beyond colorTableSize should be treated as black.
   */
  static bool decodeGifToBuffer(HalFile& input, int maxWidth, int maxHeight, uint8_t** outIndices, uint8_t** outPalette,
                                int* outPaletteEntries, int* outWidth, int* outHeight,
                                std::function<bool()> shouldAbort = nullptr);

 private:
  // GIF file format structures
  struct GifHeader {
    char signature[6];  // "GIF87a" or "GIF89a"
  };

  struct LogicalScreenDescriptor {
    uint16_t width;            // Canvas width
    uint16_t height;           // Canvas height
    uint8_t flags;             // Packed fields (color table, resolution, etc.)
    uint8_t bgColorIndex;      // Background color index
    uint8_t pixelAspectRatio;  // Pixel aspect ratio
  };

  struct ImageDescriptor {
    uint16_t left;    // X position of image on canvas
    uint16_t top;     // Y position of image on canvas
    uint16_t width;   // Image width
    uint16_t height;  // Image height
    uint8_t flags;    // Packed fields (local color table, interlace, etc.)
  };

  // Internal helper functions

  /** @brief Parse GIF file header ("GIF87a" or "GIF89a") */
  static bool parseHeader(const uint8_t*& data, size_t& size);

  /** @brief Parse logical screen descriptor (canvas size, flags) */
  static bool parseLogicalScreen(const uint8_t*& data, size_t& size, LogicalScreenDescriptor& lsd);

  /** @brief Parse image descriptor (position, size, flags) */
  static bool parseImageDescriptor(const uint8_t*& data, size_t& size, ImageDescriptor& imgDesc);

  /**
   * @brief Decompress LZW-compressed GIF image data
   *
   * Implements LZW decompression for GIF format, handling variable-width
   * codes and GIF-specific sub-block structure. Uses dynamic memory
   * allocation scaled to image size.
   *
   * @param input Open GIF file handle positioned at the first LZW sub-block
   * @param output Buffer to write decompressed palette indices
   * @param outputSize Expected output size (width * height)
   * @param width Image width (for validation)
   * @param height Image height (for validation)
   * @param colorTable RGB color palette (3 bytes per entry)
   * @param colorTableSize Number of palette entries
   * @param minCodeSize Minimum LZW code size (from GIF)
   * @param shouldAbort Optional callback to cancel decoding early
   * @return true on success, false on error
   */
  static bool decompressLZW(HalFile& input, uint8_t* output, size_t outputSize, int width, int height,
                            const uint8_t* colorTable, int colorTableSize, uint8_t minCodeSize,
                            std::function<bool()> shouldAbort);

  /**
   * @brief Write BMP file header with proper row padding calculation
   *
   * Generates a 24-bit BMP header with correct file size including
   * row padding bytes for 4-byte alignment.
   *
   * @param output Print stream to write header to
   * @param width Image width
   * @param height Image height
   */
  static void writeBmpHeader(Print& output, int width, int height);
};