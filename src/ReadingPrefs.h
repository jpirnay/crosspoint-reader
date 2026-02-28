#pragma once

#include <string>

// Per-book reading preferences stored alongside the book cache.
struct BookPrefs {
  bool embeddedStyle = true;

  bool loadFromFile(const std::string& path);
  bool saveToFile(const std::string& path) const;
};
