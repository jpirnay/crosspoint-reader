#pragma once

// Host-side Epub shim. Host tests exercise the buffer-based variants of the KOReaderSync
// mappers (findXPath...FromBuffer, findProgress...FromBuffer) which don't touch the real
// EPUB container. The production translation units still reference `Epub` / spine entries
// via their header signatures, so this shim provides a compilable stand-in. Any method
// call from a test is a bug and aborts loudly.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "Print.h"

struct EpubShimSpineEntry {
  std::string href;
  size_t cumulativeSize = 0;
  int16_t tocIndex = -1;
};

struct EpubShimTocEntry {
  std::string title;
  std::string href;
  std::string anchor;
  uint8_t level = 0;
  int16_t spineIndex = -1;
};

class Epub {
 public:
  using SpineEntry = EpubShimSpineEntry;
  using TocEntry = EpubShimTocEntry;

  static void unavailable(const char* what) {
    std::fprintf(stderr, "Epub shim: %s called in host test\n", what);
    std::abort();
  }

  const std::string& getCachePath() const { unavailable("getCachePath"); }
  const std::string& getPath() const { unavailable("getPath"); }
  int getSpineItemsCount() const { unavailable("getSpineItemsCount"); }
  SpineEntry getSpineItem(int) const { unavailable("getSpineItem"); }
  TocEntry getTocItem(int) const { unavailable("getTocItem"); }
  int getTocIndexForSpineIndex(int) const { unavailable("getTocIndexForSpineIndex"); }
  int getSpineIndexForTocIndex(int) const { unavailable("getSpineIndexForTocIndex"); }
  size_t getCumulativeSpineItemSize(int) const { unavailable("getCumulativeSpineItemSize"); }
  size_t getBookSize() const { unavailable("getBookSize"); }
  float calculateProgress(int, float) const { unavailable("calculateProgress"); }
  bool readItemContentsToStream(const std::string&, Print&, size_t) const { unavailable("readItemContentsToStream"); }
};
