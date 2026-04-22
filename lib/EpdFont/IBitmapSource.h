#pragma once

#include <SPIFFS.h>

#include <cstdint>
#include <cstring>

#include "EpdFontData.h"

/// Abstract interface for reading compressed glyph bitmap data.
/// Built-in fonts use FlashBitmapSource (zero-copy pointer into rodata).
/// Custom fonts loaded from SPIFFS use SpiffsBitmapSource (file seek+read).
class IBitmapSource {
 public:
  virtual ~IBitmapSource() = default;

  /// Read `length` compressed bytes starting at `offset` into `dst`.
  /// Returns true on success.
  virtual bool read(uint32_t offset, uint8_t* dst, uint32_t length) = 0;
};

/// Bitmap source backed by a pointer into flash (built-in fonts).
class FlashBitmapSource : public IBitmapSource {
 public:
  explicit FlashBitmapSource(const uint8_t* data) : _data(data) {}

  bool read(uint32_t offset, uint8_t* dst, uint32_t length) override {
    memcpy(dst, _data + offset, length);
    return true;
  }

 private:
  const uint8_t* _data;
};

/// Bitmap source backed by an open SPIFFS file handle (custom fonts).
/// `_bitmapOffset` is the byte position within the file where compressed
/// bitmap data begins; offsets passed to read() are relative to that base.
class SpiffsBitmapSource : public IBitmapSource {
 public:
  SpiffsBitmapSource() = default;
  ~SpiffsBitmapSource() { close(); }

  bool open(const char* path, uint32_t bitmapOffset) {
    _file = SPIFFS.open(path, FILE_READ);
    if (!_file) return false;
    _bitmapOffset = bitmapOffset;
    return true;
  }

  void close() {
    if (_file) _file.close();
  }

  bool isOpen() const { return static_cast<bool>(_file); }

  bool read(uint32_t offset, uint8_t* dst, uint32_t length) override {
    if (!_file) return false;
    if (!_file.seek(_bitmapOffset + offset)) return false;
    return _file.read(dst, length) == length;
  }

 private:
  fs::File _file;
  uint32_t _bitmapOffset = 0;
};
