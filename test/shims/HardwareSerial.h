#pragma once

#include "Print.h"

class HWCDC : public Print {
 public:
  void begin(unsigned long) {}
  operator bool() const { return true; }

  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t size) override { return size; }
  void flush() override {}
};

inline HWCDC Serial;
