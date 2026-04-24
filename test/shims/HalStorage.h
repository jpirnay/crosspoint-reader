#pragma once

// Minimal host-side HalStorage shim. The KOReaderSync code paths we exercise in tests
// (the buffer-based forward/reverse mapper variants) never touch storage — but the
// surrounding production translation units still reference Storage / FsFile, so we
// supply just-enough surface to compile. Calling any method here is a test bug and
// aborts so that accidental use surfaces immediately.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Print.h"
#include "WString.h"

class FsFile : public Print {
 public:
  bool open(const char*, int = 0) { return false; }
  bool isOpen() const { return false; }
  size_t size() const { return 0; }
  size_t position() const { return 0; }
  bool seek(size_t) { return false; }
  int available() const { return 0; }
  size_t read(void*, size_t) { return 0; }
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t size) override { return size; }
  void close() {}
};

class HalStorageShim {
 public:
  bool exists(const char*) {
    std::fprintf(stderr, "HalStorage shim: exists() called in host test\n");
    std::abort();
  }
  bool remove(const char*) {
    std::fprintf(stderr, "HalStorage shim: remove() called in host test\n");
    std::abort();
  }
  bool openFileForWrite(const char*, const std::string&, FsFile&) {
    std::fprintf(stderr, "HalStorage shim: openFileForWrite() called in host test\n");
    std::abort();
  }
  bool openFileForRead(const char*, const std::string&, FsFile&) {
    std::fprintf(stderr, "HalStorage shim: openFileForRead() called in host test\n");
    std::abort();
  }
};

inline HalStorageShim Storage;
