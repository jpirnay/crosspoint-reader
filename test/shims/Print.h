#pragma once

#include <cstddef>
#include <cstdint>

class Print {
 public:
  virtual ~Print() = default;

  virtual size_t write(uint8_t) = 0;

  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t written = 0;
    for (size_t i = 0; i < size; i++) {
      written += write(buffer[i]);
    }
    return written;
  }

  virtual void flush() {}
};
