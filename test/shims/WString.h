#pragma once

#include <cstddef>
#include <cstring>

// Minimal host-side stand-in for the Arduino framework's WString/String.
// Only provides the surface that header-only inline overloads in this repo
// touch (c_str/length). Host tests never construct a String.
class String {
 public:
  String() = default;
  String(const char* s) : _buf(s ? s : "") {}

  const char* c_str() const { return _buf; }
  size_t length() const { return std::strlen(_buf); }

 private:
  const char* _buf = "";
};
