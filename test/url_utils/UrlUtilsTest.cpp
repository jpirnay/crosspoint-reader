#include <cstdio>
#include <string>

#include "../../src/util/UrlUtils.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_EQ(a, b)                                                         \
  do {                                                                          \
    if ((a) != (b)) {                                                           \
      fprintf(stderr, "  FAIL: %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
      testsFailed++;                                                            \
      return;                                                                   \
    }                                                                           \
  } while (0)

#define PASS() testsPassed++

void testAbsoluteUrlRemainsUnchanged() {
  printf("testAbsoluteUrlRemainsUnchanged...\n");
  ASSERT_EQ(
      UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml", "https://example.com/books/download/test.epub"),
      "https://example.com/books/download/test.epub");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml", "//cdn.example.com/books/test.epub"),
            "https://cdn.example.com/books/test.epub");
  PASS();
}

void testRootRelativeUrlUsesHostRoot() {
  printf("testRootRelativeUrlUsesHostRoot...\n");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml", "/books/download/test.epub"),
            "https://catalog.example.com/books/download/test.epub");
  PASS();
}

void testRelativeUrlUsesFeedDirectory() {
  printf("testRelativeUrlUsesFeedDirectory...\n");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml", "download/test.epub"),
            "https://catalog.example.com/opds/download/test.epub");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/sub", "download/test.epub"),
            "https://catalog.example.com/opds/download/test.epub");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/sub/", "download/test.epub"),
            "https://catalog.example.com/opds/sub/download/test.epub");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml", "images//cover.jpg"),
            "https://catalog.example.com/opds/images//cover.jpg");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml", "subdir/"),
            "https://catalog.example.com/opds/subdir/");
  PASS();
}

void testParentRelativeUrlResolvesCorrectly() {
  printf("testParentRelativeUrlResolvesCorrectly...\n");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/sub/feed.xml", "../download/test.epub"),
            "https://catalog.example.com/opds/download/test.epub");
  PASS();
}

void testUrlsWithQueryAndFragmentResolveCorrectly() {
  printf("testUrlsWithQueryAndFragmentResolveCorrectly...\n");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/sub?auth=1", "download/test.epub"),
            "https://catalog.example.com/opds/download/test.epub");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/sub/#section", "download/test.epub?format=epub#start"),
            "https://catalog.example.com/opds/sub/download/test.epub?format=epub#start");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml?auth=1#top", "download/test.epub"),
            "https://catalog.example.com/opds/download/test.epub");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml", "/books?id=/../cover"),
            "https://catalog.example.com/books?id=/../cover");
  ASSERT_EQ(
      UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml", "download/test.epub?next=/../cover#frag/../x"),
      "https://catalog.example.com/opds/download/test.epub?next=/../cover#frag/../x");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml", "download/test.epub#frag?x=1"),
            "https://catalog.example.com/opds/download/test.epub#frag?x=1");
  PASS();
}

void testRootRelativeUrlsWithQueryAndFragmentResolveCorrectly() {
  printf("testRootRelativeUrlsWithQueryAndFragmentResolveCorrectly...\n");
  ASSERT_EQ(
      UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml?auth=1", "/books/download/test.epub?format=epub"),
      "https://catalog.example.com/books/download/test.epub?format=epub");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/sub/#section", "/books/download/test.epub#start"),
            "https://catalog.example.com/books/download/test.epub#start");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/sub?auth=1#top",
                               "/books/download/test.epub?format=epub#start"),
            "https://catalog.example.com/books/download/test.epub?format=epub#start");
  PASS();
}

void testHostOnlyBaseUrlsWithQueryAndFragmentResolveCorrectly() {
  printf("testHostOnlyBaseUrlsWithQueryAndFragmentResolveCorrectly...\n");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com?auth=1", "download/test.epub"),
            "https://catalog.example.com/download/test.epub");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com#top", "/books/download/test.epub"),
            "https://catalog.example.com/books/download/test.epub");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com?auth=1", "?page=2"), "https://catalog.example.com?page=2");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com#top", "#latest"), "https://catalog.example.com#latest");
  PASS();
}

void testQueryAndFragmentReferencesAreRfcCompliant() {
  printf("testQueryAndFragmentReferencesAreRfcCompliant...\n");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml?auth=1", "#top"),
            "https://catalog.example.com/opds/feed.xml?auth=1#top");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com/opds/feed.xml?auth=1#old", "?page=2"),
            "https://catalog.example.com/opds/feed.xml?page=2");
  ASSERT_EQ(UrlUtils::buildUrl("https://catalog.example.com?auth=1#old", "#latest"),
            "https://catalog.example.com?auth=1#latest");
  PASS();
}

void testExtractHostnameHandlesPortsAndIpv6() {
  printf("testExtractHostnameHandlesPortsAndIpv6...\n");
  ASSERT_EQ(UrlUtils::extractHostname("https://catalog.example.com:8080/path"), "catalog.example.com");
  ASSERT_EQ(UrlUtils::extractHostname("catalog.example.com:8080/opds"), "catalog.example.com");
  ASSERT_EQ(UrlUtils::extractHostname("https://user:pass@[2001:db8::1]:8080/path?x=1#frag"), "2001:db8::1");
  ASSERT_EQ(UrlUtils::extractHostname("http://[fe80::1234]#top"), "fe80::1234");
  PASS();
}

void testExtractHostnameRejectsMalformedInputs() {
  printf("testExtractHostnameRejectsMalformedInputs...\n");
  ASSERT_EQ(UrlUtils::extractHostname("https:///path"), "");
  ASSERT_EQ(UrlUtils::extractHostname("http://[]"), "");
  ASSERT_EQ(UrlUtils::extractHostname("http://@/x"), "");
  ASSERT_EQ(UrlUtils::extractHostname("http://[::1"), "");
  ASSERT_EQ(UrlUtils::extractHostname("http://[::1]x"), "");
  ASSERT_EQ(UrlUtils::extractHostname("mailto:user@example.com"), "");
  ASSERT_EQ(UrlUtils::extractHostname("urn:isbn:1234567890"), "");
  PASS();
}

int main() {
  printf("=== OPDS URL Utils Tests ===\n\n");

  testAbsoluteUrlRemainsUnchanged();
  testRootRelativeUrlUsesHostRoot();
  testRelativeUrlUsesFeedDirectory();
  testParentRelativeUrlResolvesCorrectly();
  testUrlsWithQueryAndFragmentResolveCorrectly();
  testRootRelativeUrlsWithQueryAndFragmentResolveCorrectly();
  testHostOnlyBaseUrlsWithQueryAndFragmentResolveCorrectly();
  testQueryAndFragmentReferencesAreRfcCompliant();
  testExtractHostnameHandlesPortsAndIpv6();
  testExtractHostnameRejectsMalformedInputs();

  printf("\n=== Results: %d passed, %d failed ===\n", testsPassed, testsFailed);
  return testsFailed > 0 ? 1 : 0;
}
