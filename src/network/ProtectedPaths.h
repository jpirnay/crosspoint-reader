#pragma once

#include <string.h>
#include <strings.h>

#include <string>

// Credential stores no web request (file manager, plugin endpoints) or plugin
// manifest may read or write: their passwords are obfuscated with a device
// key, so the files must stay on the device. Mirrors the getFilePath() of
// WifiCredentialStore, OpdsServerStore and KOReaderCredentialStore. Prefix
// match, so the stores' temp/backup siblings are covered too.
namespace protectedpaths {

inline constexpr const char* SENSITIVE_FILES[] = {"/.crosspoint/wifi.json", "/.crosspoint/opds.json",
                                                  "/.crosspoint/koreader.json"};

inline bool isHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }

// SdFat also opens a name by its generated 8.3 alias (".crosspoint" is
// CROSSP~1), which no spelling check can map back to the long name. Check every
// '~', since the long name may hold one of its own: a digit after it marks an
// alias, and so do four hex digits that end the base (SdFat's tail when ~1 is
// taken, FatFile::makeUniqueSfn).
inline bool isShortAlias(const char* first, const char* last) {
  const char* dot = static_cast<const char*>(memchr(first, '.', static_cast<size_t>(last - first)));
  const char* baseEnd = dot ? dot : last;
  const size_t ext = dot ? static_cast<size_t>(last - dot - 1) : 0;
  if (baseEnd - first > 8 || ext > 3) return false;
  for (const char* tilde = first; tilde < last; tilde++) {
    if (*tilde != '~') continue;
    if (tilde + 1 < last && tilde[1] >= '0' && tilde[1] <= '9') return true;
    if (baseEnd - tilde == 5 && isHexDigit(tilde[1]) && isHexDigit(tilde[2]) && isHexDigit(tilde[3]) &&
        isHexDigit(tilde[4])) {
      return true;
    }
  }
  return false;
}

inline bool isSensitivePath(const char* path) {
  // Canonicalise first ("//", "/./" and ".." would otherwise dodge the prefix
  // match while still opening the same file).
  std::string canon;
  canon.reserve(strlen(path) + 1);
  int depth = 0;
  for (const char* seg = path; *seg;) {
    while (*seg == '/') seg++;
    const char* end = strchr(seg, '/');
    const size_t len = end ? static_cast<size_t>(end - seg) : strlen(seg);
    if (len == 2 && seg[0] == '.' && seg[1] == '.') {
      const size_t slash = canon.rfind('/');
      canon.erase(slash == std::string::npos ? 0 : slash);
      if (depth > 0) depth--;
    } else {
      // SdFat skips leading spaces and drops trailing dots and spaces when it
      // opens a name, so compare the name it would actually open.
      const char* first = seg;
      const char* last = seg + len;
      while (first < last && *first == ' ') first++;
      while (last > first && (last[-1] == '.' || last[-1] == ' ')) last--;
      // The stores sit two levels deep; an alias further down cannot reach one.
      if (depth < 2 && isShortAlias(first, last)) return true;
      if (first < last) {
        canon += '/';
        canon.append(first, static_cast<size_t>(last - first));
        depth++;
      }
    }
    seg += len;
  }
  for (const char* file : SENSITIVE_FILES) {
    // FAT names are case-insensitive.
    if (strncasecmp(canon.c_str(), file, strlen(file)) == 0) return true;
  }
  return false;
}

// A plugin-supplied path (web request or device.json): absolute, no parent
// refs, not a credential store.
inline bool isPluginPath(const std::string& p) {
  return p.size() > 1 && p[0] == '/' && p.find("..") == std::string::npos && !isSensitivePath(p.c_str());
}

}  // namespace protectedpaths
