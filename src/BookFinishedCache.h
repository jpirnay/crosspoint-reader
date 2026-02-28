#pragma once

#include <array>
#include <cstdint>
#include <string>

class BookFinishedCache {
 public:
  static BookFinishedCache& getInstance();

  bool tryGet(const std::string& bookPath, bool& finished);
  bool resolve(const std::string& bookPath, bool& finished);
  void put(const std::string& bookPath, bool finished);
  void clear();

 private:
  struct Entry {
    uint32_t key = 0;
    int8_t value = 0;
    uint32_t age = 0;
    bool used = false;
  };

  static constexpr size_t MAX_ENTRIES = 64;

  std::array<Entry, MAX_ENTRIES> entries = {};
  uint32_t ageCounter = 0;

  static uint32_t makeKey(const std::string& bookPath);
  int findIndex(uint32_t key) const;
  int findEvictionIndex() const;
  static bool loadFinishedFromStats(const std::string& bookPath, bool& finished);
};

#define BOOK_FINISHED_CACHE BookFinishedCache::getInstance()