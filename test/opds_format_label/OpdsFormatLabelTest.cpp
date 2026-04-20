#include <cstdio>
#include <string>
#include <vector>

#include "../../src/activities/browser/OpdsFormatLabel.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_TRUE(cond)                                                \
  do {                                                                   \
    if (!(cond)) {                                                       \
      fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      testsFailed++;                                                     \
      return;                                                            \
    }                                                                    \
  } while (0)

#define ASSERT_EQ(a, b)                                                         \
  do {                                                                          \
    if ((a) != (b)) {                                                           \
      fprintf(stderr, "  FAIL: %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
      testsFailed++;                                                            \
      return;                                                                   \
    }                                                                           \
  } while (0)

#define PASS() testsPassed++

namespace {
OpdsAcquisitionLink makeLink(const char* href, const char* formatKey) {
  return OpdsAcquisitionLink{href, "application/epub+zip", formatKey, ".epub"};
}
}  // namespace

void testUniqueFormatUsesBaseLabel() {
  printf("testUniqueFormatUsesBaseLabel...\n");
  const auto link = makeLink("/books/example.epub", "epub");
  const std::vector<OpdsAcquisitionLink> links{link};

  ASSERT_EQ(opdsFormatSelectionLabel(link, links, "catalog.example.com"), "EPUB");
  PASS();
}

void testDuplicateAbsoluteUrlsIncludeHostname() {
  printf("testDuplicateAbsoluteUrlsIncludeHostname...\n");
  const auto primary = makeLink("https://mirror-a.example.com/books/example.epub", "epub");
  const auto secondary = makeLink("https://mirror-b.example.com/books/example.epub", "epub");
  const std::vector<OpdsAcquisitionLink> links{primary, secondary};

  ASSERT_EQ(opdsFormatSelectionLabel(primary, links, "catalog.example.com"), "EPUB - mirror-a.example.com");
  ASSERT_EQ(opdsFormatSelectionLabel(secondary, links, "catalog.example.com"), "EPUB - mirror-b.example.com");
  PASS();
}

void testDuplicateRootRelativeUrlsUseServerHostname() {
  printf("testDuplicateRootRelativeUrlsUseServerHostname...\n");
  const auto primary = makeLink("/opds/download/1/epub", "epub");
  const auto secondary = makeLink("/opds/download/2/epub", "epub");
  const std::vector<OpdsAcquisitionLink> links{primary, secondary};

  ASSERT_EQ(opdsFormatSelectionLabel(primary, links, "https://catalog.example.com/opds"),
            "EPUB - catalog.example.com (1)");
  ASSERT_EQ(opdsFormatSelectionLabel(secondary, links, "https://catalog.example.com/opds"),
            "EPUB - catalog.example.com (2)");
  PASS();
}

void testDuplicateRelativeUrlsUseServerHostname() {
  printf("testDuplicateRelativeUrlsUseServerHostname...\n");
  const auto primary = makeLink("download/1.epub", "epub");
  const auto secondary = makeLink("download/2.epub", "epub");
  const std::vector<OpdsAcquisitionLink> links{primary, secondary};

  ASSERT_EQ(opdsFormatSelectionLabel(primary, links, "catalog.example.com/opds"), "EPUB - catalog.example.com (1)");
  ASSERT_EQ(opdsFormatSelectionLabel(secondary, links, "catalog.example.com/opds"), "EPUB - catalog.example.com (2)");
  PASS();
}

void testDuplicateAbsoluteUrlsSameHostnameIncludeNumbering() {
  printf("testDuplicateAbsoluteUrlsSameHostnameIncludeNumbering...\n");
  const auto primary = makeLink("https://mirror.example.com/books/example.epub", "epub");
  const auto secondary = makeLink("https://mirror.example.com/books/example-copy.epub", "epub");
  const std::vector<OpdsAcquisitionLink> links{primary, secondary};

  ASSERT_EQ(opdsFormatSelectionLabel(primary, links, "catalog.example.com"), "EPUB - mirror.example.com (1)");
  ASSERT_EQ(opdsFormatSelectionLabel(secondary, links, "catalog.example.com"), "EPUB - mirror.example.com (2)");
  PASS();
}

void testBatchLabelBuilderMatchesPerLinkLabels() {
  printf("testBatchLabelBuilderMatchesPerLinkLabels...\n");
  const auto first = makeLink("https://mirror.example.com/books/example.epub", "epub");
  const auto second = makeLink("https://mirror.example.com/books/example-copy.epub", "epub");
  const auto third = makeLink("/books/example.txt", "txt");
  const std::vector<OpdsAcquisitionLink> links{first, second, third};

  const auto labels = buildOpdsFormatSelectionLabels(links, "https://catalog.example.com/opds");
  ASSERT_EQ(labels.size(), static_cast<size_t>(3));
  ASSERT_EQ(labels[0], opdsFormatSelectionLabel(first, links, "https://catalog.example.com/opds"));
  ASSERT_EQ(labels[1], opdsFormatSelectionLabel(second, links, "https://catalog.example.com/opds"));
  ASSERT_EQ(labels[2], opdsFormatSelectionLabel(third, links, "https://catalog.example.com/opds"));
  PASS();
}

int main() {
  printf("=== OPDS Format Label Tests ===\n\n");

  testUniqueFormatUsesBaseLabel();
  testDuplicateAbsoluteUrlsIncludeHostname();
  testDuplicateRootRelativeUrlsUseServerHostname();
  testDuplicateRelativeUrlsUseServerHostname();
  testDuplicateAbsoluteUrlsSameHostnameIncludeNumbering();
  testBatchLabelBuilderMatchesPerLinkLabels();

  printf("\n=== Results: %d passed, %d failed ===\n", testsPassed, testsFailed);
  return testsFailed > 0 ? 1 : 0;
}
