#include <OpdsParser.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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

#define ASSERT_SIZE(a, b)                                                                                         \
  do {                                                                                                            \
    if ((a) != (b)) {                                                                                             \
      fprintf(stderr, "  FAIL: %s:%d: %s == %zu, expected %zu\n", __FILE__, __LINE__, #a, static_cast<size_t>(a), \
              static_cast<size_t>(b));                                                                            \
      testsFailed++;                                                                                              \
      return;                                                                                                     \
    }                                                                                                             \
  } while (0)

#define PASS() testsPassed++

namespace {
bool parseSingleBookEntry(OpdsEntry& entryOut, const char* href, const char* type = "application/epub+zip") {
  const std::string xml = std::string(R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-1</id>
    <link rel="http://opds-spec.org/acquisition" type=")") +
                          type + R"(" href=")" + href + R"("/>
  </entry>
</feed>)";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  if (parser.error()) {
    fprintf(stderr, "  FAIL: %s:%d: parser.error()\n", __FILE__, __LINE__);
    testsFailed++;
    return false;
  }
  const auto& entries = parser.getEntries();
  if (entries.size() != 1) {
    fprintf(stderr, "  FAIL: %s:%d: entries.size() == %zu, expected 1\n", __FILE__, __LINE__, entries.size());
    testsFailed++;
    return false;
  }
  entryOut = entries.front();
  return true;
}

bool assertSingleFormat(const OpdsEntry& entry, const char* formatKey, const char* fileExtension) {
  if (entry.type != OpdsEntryType::BOOK) {
    fprintf(stderr, "  FAIL: %s:%d: entry.type != OpdsEntryType::BOOK\n", __FILE__, __LINE__);
    testsFailed++;
    return false;
  }
  if (entry.acquisitionLinks.size() != 1) {
    fprintf(stderr, "  FAIL: %s:%d: entry.acquisitionLinks.size() == %zu, expected 1\n", __FILE__, __LINE__,
            entry.acquisitionLinks.size());
    testsFailed++;
    return false;
  }
  if (entry.acquisitionLinks[0].formatKey != formatKey) {
    fprintf(stderr, "  FAIL: %s:%d: formatKey actual='%s' expected='%s'\n", __FILE__, __LINE__,
            entry.acquisitionLinks[0].formatKey.c_str(), formatKey);
    testsFailed++;
    return false;
  }
  if (entry.acquisitionLinks[0].fileExtension != fileExtension) {
    fprintf(stderr, "  FAIL: %s:%d: fileExtension actual='%s' expected='%s'\n", __FILE__, __LINE__,
            entry.acquisitionLinks[0].fileExtension.c_str(), fileExtension);
    testsFailed++;
    return false;
  }
  return true;
}
}  // namespace

void testEpubExtension() {
  printf("testEpubExtension...\n");
  OpdsEntry entry;
  if (!parseSingleBookEntry(entry, "/books/example.epub")) return;
  ASSERT_TRUE(assertSingleFormat(entry, "epub", ".epub"));
  PASS();
}

void testKepubDoubleExtension() {
  printf("testKepubDoubleExtension...\n");
  OpdsEntry entry;
  if (!parseSingleBookEntry(entry, "/books/example.kepub.epub")) return;
  ASSERT_TRUE(assertSingleFormat(entry, "kepub", ".kepub.epub"));
  PASS();
}

void testBareKepubExtension() {
  printf("testBareKepubExtension...\n");
  OpdsEntry entry;
  if (!parseSingleBookEntry(entry, "/books/example.kepub")) return;
  ASSERT_TRUE(assertSingleFormat(entry, "kepub", ".kepub.epub"));
  PASS();
}

void testSlashTerminatedKepubPath() {
  printf("testSlashTerminatedKepubPath...\n");
  OpdsEntry entry;
  if (!parseSingleBookEntry(entry, "/opds/download/6516/kepub/")) return;
  ASSERT_TRUE(assertSingleFormat(entry, "kepub", ".kepub.epub"));
  PASS();
}

void testSlashTerminatedEpubPath() {
  printf("testSlashTerminatedEpubPath...\n");
  OpdsEntry entry;
  if (!parseSingleBookEntry(entry, "/opds/download/6516/epub/")) return;
  ASSERT_TRUE(assertSingleFormat(entry, "epub", ".epub"));
  PASS();
}

void testDistinctAcquisitionFormatsRemainSeparate() {
  printf("testDistinctAcquisitionFormatsRemainSeparate...\n");
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-2</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.kepub.epub"/>
    <link rel="http://opds-spec.org/acquisition" type="text/plain" href="/books/example.txt"/>
    <link rel="http://opds-spec.org/acquisition" type="text/markdown" href="/books/example.md"/>
  </entry>
</feed>)";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  const auto& entries = parser.getEntries();
  ASSERT_SIZE(entries.size(), 1);
  const auto& links = entries.front().acquisitionLinks;
  ASSERT_SIZE(links.size(), 4);
  ASSERT_EQ(links[0].formatKey, "epub");
  ASSERT_EQ(links[1].formatKey, "kepub");
  ASSERT_EQ(links[2].formatKey, "txt");
  ASSERT_EQ(links[3].formatKey, "md");
  PASS();
}

void testUnsupportedMimeType() {
  printf("testUnsupportedMimeType...\n");
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-3</id>
    <link rel="http://opds-spec.org/acquisition" type="application/x-mobipocket-ebook" href="/books/example.mobi"/>
  </entry>
</feed>)";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  ASSERT_SIZE(parser.getEntries().size(), 0);
  PASS();
}

void testEmptyHrefOrType() {
  printf("testEmptyHrefOrType...\n");
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Empty Href</title>
    <author><name>Example Author</name></author>
    <id>book-4</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href=""/>
  </entry>
  <entry>
    <title>Empty Type</title>
    <author><name>Example Author</name></author>
    <id>book-5</id>
    <link rel="http://opds-spec.org/acquisition" type="" href="/books/example.epub"/>
  </entry>
  <entry>
    <title>Missing Href</title>
    <author><name>Example Author</name></author>
    <id>book-6</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip"/>
  </entry>
  <entry>
    <title>Missing Type</title>
    <author><name>Example Author</name></author>
    <id>book-7</id>
    <link rel="http://opds-spec.org/acquisition" href="/books/example.epub"/>
  </entry>
</feed>)";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  const auto& entries = parser.getEntries();
  ASSERT_SIZE(entries.size(), 0);
  PASS();
}

void testDuplicateAcquisitionLinks() {
  printf("testDuplicateAcquisitionLinks...\n");
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-8</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example-copy.epub"/>
  </entry>
</feed>)";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  const auto& entries = parser.getEntries();
  ASSERT_SIZE(entries.size(), 1);
  const auto& links = entries.front().acquisitionLinks;
  ASSERT_SIZE(links.size(), 2);
  ASSERT_EQ(links[0].formatKey, "epub");
  ASSERT_EQ(links[0].href, "/books/example.epub");
  ASSERT_EQ(links[1].formatKey, "epub");
  ASSERT_EQ(links[1].href, "/books/example-copy.epub");
  PASS();
}

void testIdenticalHrefAcquisitionLinksAreDeduplicated() {
  printf("testIdenticalHrefAcquisitionLinksAreDeduplicated...\n");
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-9</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
  </entry>
</feed>)";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  const auto& entries = parser.getEntries();
  ASSERT_SIZE(entries.size(), 1);
  const auto& links = entries.front().acquisitionLinks;
  ASSERT_SIZE(links.size(), 1);
  ASSERT_EQ(links[0].formatKey, "epub");
  ASSERT_EQ(links[0].href, "/books/example.epub");
  PASS();
}

void testSlashVariantHrefAcquisitionLinksAreDeduplicated() {
  printf("testSlashVariantHrefAcquisitionLinksAreDeduplicated...\n");
  const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>Example Book</title>
    <author><name>Example Author</name></author>
    <id>book-10</id>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub"/>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/books/example.epub/"/>
  </entry>
</feed>)";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml), strlen(xml));
  parser.flush();

  ASSERT_TRUE(!parser.error());
  const auto& entries = parser.getEntries();
  ASSERT_SIZE(entries.size(), 1);
  const auto& links = entries.front().acquisitionLinks;
  ASSERT_SIZE(links.size(), 1);
  ASSERT_EQ(links[0].formatKey, "epub");
  ASSERT_EQ(links[0].href, "/books/example.epub");
  PASS();
}

int main() {
  printf("=== OPDS Parser Tests ===\n\n");

  testEpubExtension();
  testKepubDoubleExtension();
  testBareKepubExtension();
  testSlashTerminatedKepubPath();
  testSlashTerminatedEpubPath();
  testDistinctAcquisitionFormatsRemainSeparate();
  testUnsupportedMimeType();
  testEmptyHrefOrType();
  testDuplicateAcquisitionLinks();
  testIdenticalHrefAcquisitionLinksAreDeduplicated();
  testSlashVariantHrefAcquisitionLinksAreDeduplicated();

  printf("\n=== Results: %d passed, %d failed ===\n", testsPassed, testsFailed);
  return testsFailed > 0 ? 1 : 0;
}
