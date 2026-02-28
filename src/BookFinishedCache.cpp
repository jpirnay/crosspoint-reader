#include "BookFinishedCache.h"

#include <functional>

#include "ReadingStats.h"

BookFinishedCache& BookFinishedCache::getInstance() {
  static BookFinishedCache instance;
  return instance;
}

uint32_t BookFinishedCache::makeKey(const std::string& bookPath) {
  return static_cast<uint32_t>(std::hash<std::string>{}(bookPath));
}

int BookFinishedCache::findIndex(const uint32_t key) const {
  for (size_t i = 0; i < entries.size(); i++) {
    if (entries[i].used && entries[i].key == key) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int BookFinishedCache::findEvictionIndex() const {
  int freeIndex = -1;
  uint32_t oldestAge = UINT32_MAX;
  int oldestIndex = 0;

  for (size_t i = 0; i < entries.size(); i++) {
    if (!entries[i].used) {
      freeIndex = static_cast<int>(i);
      break;
    }
    if (entries[i].age < oldestAge) {
      oldestAge = entries[i].age;
      oldestIndex = static_cast<int>(i);
    }
  }

  return (freeIndex >= 0) ? freeIndex : oldestIndex;
}

bool BookFinishedCache::loadFinishedFromStats(const std::string& bookPath, bool& finished) {
  const std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(bookPath));
  BookStats stats;
  if (stats.loadFromFile(cachePath + "/stats.json")) {
    finished = stats.finished;
    return true;
  }

  finished = false;
  return false;
}

bool BookFinishedCache::tryGet(const std::string& bookPath, bool& finished) {
  const uint32_t key = makeKey(bookPath);
  const int idx = findIndex(key);
  if (idx < 0) {
    return false;
  }

  entries[idx].age = ++ageCounter;
  finished = (entries[idx].value == 1);
  return true;
}

bool BookFinishedCache::resolve(const std::string& bookPath, bool& finished) {
  if (tryGet(bookPath, finished)) {
    return true;
  }

  loadFinishedFromStats(bookPath, finished);
  put(bookPath, finished);
  return true;
}

void BookFinishedCache::put(const std::string& bookPath, const bool finished) {
  const uint32_t key = makeKey(bookPath);
  int idx = findIndex(key);
  if (idx < 0) {
    idx = findEvictionIndex();
    entries[idx].used = true;
    entries[idx].key = key;
  }

  entries[idx].value = finished ? 1 : 0;
  entries[idx].age = ++ageCounter;
}

void BookFinishedCache::clear() {
  for (auto& entry : entries) {
    entry = Entry{};
  }
  ageCounter = 0;
}
