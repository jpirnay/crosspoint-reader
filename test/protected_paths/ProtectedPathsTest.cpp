#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "network/ProtectedPaths.h"

using protectedpaths::isPluginPath;
using protectedpaths::isSensitivePath;

TEST(ProtectedPaths, CredentialStoresAreSensitive) {
  EXPECT_TRUE(isSensitivePath("/.crosspoint/wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/opds.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/koreader.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/wifi.json.tmp"));
  EXPECT_TRUE(isSensitivePath("/.CrossPoint/WIFI.json"));
}

TEST(ProtectedPaths, SpellingVariantsCannotDodgeTheMatch) {
  EXPECT_TRUE(isSensitivePath("/.crosspoint/./wifi.json"));
  EXPECT_TRUE(isSensitivePath("//.crosspoint//wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/plugins/../wifi.json"));
  EXPECT_TRUE(isSensitivePath(".crosspoint/opds.json"));
}

TEST(ProtectedPaths, NamesSdFatWouldTrimOrAliasStillMatch) {
  EXPECT_TRUE(isSensitivePath("/.crosspoint/wifi.json."));
  EXPECT_TRUE(isSensitivePath("/.crosspoint./wifi.json"));
  EXPECT_TRUE(isSensitivePath("/ .crosspoint/ wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint ./opds.json"));
  EXPECT_TRUE(isSensitivePath("/CROSSP~1/wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.crosspoint/WIFI~1.JSO"));
  EXPECT_TRUE(isSensitivePath("/CROSSP~1/WIFI~1.JSO"));
  EXPECT_TRUE(isSensitivePath("/CROSSP~1"));                 // WebDAV PROPFIND would list the stores
  EXPECT_FALSE(isSensitivePath("/Books/Photos/IMG~1.JPG"));  // too deep to reach a store
  EXPECT_FALSE(isSensitivePath("/Books/a~b.epub"));
}

TEST(ProtectedPaths, EverythingElseStaysReachable) {
  EXPECT_FALSE(isSensitivePath("/.crosspoint/settings.json"));
  EXPECT_FALSE(isSensitivePath("/.crosspoint/library.idx"));
  EXPECT_FALSE(isSensitivePath("/.crosspoint/month-wallpaper.json"));
  EXPECT_FALSE(isSensitivePath("/.fonts/Noto/Noto_14.cpfont"));
  EXPECT_FALSE(isSensitivePath("/Books/wifi.json"));
}

TEST(ProtectedPaths, PluginPathRules) {
  EXPECT_FALSE(isPluginPath("/.crosspoint/plugins/../wifi.json"));
  EXPECT_FALSE(isPluginPath("/.crosspoint/./koreader.json"));
  EXPECT_FALSE(isPluginPath("relative/path"));
  EXPECT_FALSE(isPluginPath("/"));
  EXPECT_TRUE(isPluginPath("/.crosspoint/month-wallpaper.json"));
  EXPECT_TRUE(isPluginPath("/Books/new.epub"));
}

TEST(ProtectedPaths, EveryHashedAliasTailIsAnAlias) {
  // When ~1 is taken, SdFat ends the base with ~ and four hex digits of millis()
  // (FatFile::makeUniqueSfn), so every one of the 65536 tails is an alias.
  int misses = 0;
  std::string firstMiss;
  for (unsigned value = 0; value <= 0xFFFF; value++) {
    char upper[5];
    char lower[5];
    snprintf(upper, sizeof(upper), "%04X", value);
    snprintf(lower, sizeof(lower), "%04x", value);
    for (const std::string& path :
         {"/CRO~" + std::string(upper) + "/wifi.json", "/cro~" + std::string(lower) + "/opds.json",
          "/.crosspoint/KOR~" + std::string(upper) + ".JSO", "/AB~" + std::string(upper)}) {
      if (!isSensitivePath(path.c_str()) && misses++ == 0) firstMiss = path;
    }
  }
  EXPECT_EQ(misses, 0) << "first miss: " << firstMiss;
}

TEST(ProtectedPaths, EveryDecimalAliasTailIsAnAlias) {
  // SdFat writes ~1; other FAT drivers number on, shortening the base to keep 8 characters.
  int misses = 0;
  std::string firstMiss;
  for (unsigned n = 1; n <= 999999; n++) {
    const std::string tail = "~" + std::to_string(n);
    const size_t keep = 8 - tail.size();
    for (const std::string& path : {"/" + std::string("CROSSPOI").substr(0, keep) + tail + "/wifi.json",
                                    "/.crosspoint/" + std::string("KOREADER").substr(0, keep) + tail + ".JSO"}) {
      if (!isSensitivePath(path.c_str()) && misses++ == 0) firstMiss = path;
    }
  }
  EXPECT_EQ(misses, 0) << "first miss: " << firstMiss;
}

TEST(ProtectedPaths, EveryTildeInANameIsChecked) {
  // An alias keeps a '~' of its long name: ".~lock.report.odt#" is ~LOCK_~1.ODT, or ~LO~HHHH.ODT.
  EXPECT_TRUE(isSensitivePath("/~LOCK_~1.ODT"));
  EXPECT_TRUE(isSensitivePath("/~LO~A3F2.ODT"));
}

TEST(ProtectedPaths, TildeNamesThatAreNotAliasesStayReachable) {
  EXPECT_FALSE(isSensitivePath("/Books/ab~cde.txt"));           // three hex digits
  EXPECT_FALSE(isSensitivePath("/Books/ab~cdef1.txt"));         // hex digits do not end the base
  EXPECT_FALSE(isSensitivePath("/Books/my~book.txt"));          // not hex digits
  EXPECT_FALSE(isSensitivePath("/Books/CRO~A3F2.epub"));        // extension too long for 8.3
  EXPECT_FALSE(isSensitivePath("/Books/Photos/CRO~A3F2.JPG"));  // too deep to reach a store
}
