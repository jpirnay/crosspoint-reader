// Host-side regression tests for the KOReader-sync XPath mappers.
//
// These exercise the *buffer* entry points in ChapterXPathForwardMapper /
// ChapterXPathReverseMapper, which accept a literal XHTML string. That keeps the tests
// independent of the EPUB decompression / SD storage stack while still covering the
// parser state machines and the full-ancestry XPath emission.
//
// Coverage focus (rationale):
// - flat vs. wrapped <p> structures emit different XPath shapes;
// - the 1.38 regression was specifically about LOSING intra-paragraph precision on flat
//   EPUBs, so we directly assert that /text()[K].N suffixes reappear when
//   intraSpineProgress is supplied;
// - round-tripping (forward then reverse) must return a progress close to the input so
//   the real upload/download cycle keeps the reader in place.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "../../lib/KOReaderSync/ChapterXPathForwardMapper.h"
#include "../../lib/KOReaderSync/ChapterXPathReverseMapper.h"

using ChapterXPathIndexerInternal::findProgressForXPathFromBuffer;
using ChapterXPathIndexerInternal::findXPathForParagraphFromBuffer;
using ChapterXPathIndexerInternal::findXPathForProgressFromBuffer;

static int testsPassed = 0;
static int testsFailed = 0;

#define FAIL_MSG(fmt, ...)                                                 \
  do {                                                                     \
    std::fprintf(stderr, "  FAIL: %s:%d: " fmt "\n", __FILE__, __LINE__,   \
                 ##__VA_ARGS__);                                           \
    testsFailed++;                                                         \
    return;                                                                \
  } while (0)

#define ASSERT_TRUE(cond)                     \
  do {                                        \
    if (!(cond)) FAIL_MSG("%s is false", #cond); \
  } while (0)

#define ASSERT_EQ(a, b)                                                     \
  do {                                                                      \
    const auto __a = (a);                                                   \
    const auto __b = (b);                                                   \
    if (!(__a == __b)) {                                                    \
      FAIL_MSG("%s != %s", #a, #b);                                         \
    }                                                                       \
  } while (0)

#define ASSERT_STR_EQ(a, b)                                                                \
  do {                                                                                     \
    const std::string __a = (a);                                                           \
    const std::string __b = (b);                                                           \
    if (__a != __b) {                                                                      \
      std::fprintf(stderr, "  FAIL: %s:%d: got='%s' expected='%s'\n", __FILE__, __LINE__,  \
                   __a.c_str(), __b.c_str());                                              \
      testsFailed++;                                                                       \
      return;                                                                              \
    }                                                                                      \
  } while (0)

#define ASSERT_CLOSE(a, b, eps)                                                                              \
  do {                                                                                                       \
    const float __a = static_cast<float>(a);                                                                 \
    const float __b = static_cast<float>(b);                                                                 \
    if (std::fabs(__a - __b) > (eps)) {                                                                      \
      std::fprintf(stderr, "  FAIL: %s:%d: |%g - %g| > %g\n", __FILE__, __LINE__, __a, __b,                  \
                   static_cast<double>(eps));                                                                \
      testsFailed++;                                                                                         \
      return;                                                                                                \
    }                                                                                                        \
  } while (0)

#define PASS() testsPassed++

namespace {

// Flat chapter: paragraphs are direct <body> children. This is the common shape and the one
// where the 1.37 → 1.38 regression was visible.
const std::string kFlatChapter = R"(<?xml version="1.0" encoding="utf-8"?>
<html><body>
<p>Alpha beta gamma delta epsilon.</p>
<p>Paragraph two has slightly more text, long enough to host a mid-paragraph offset.</p>
<p>Third paragraph with three sentences. Still easy to reason about. The end.</p>
<p>Fourth paragraph.</p>
</body></html>)";

// Wrapped chapter: paragraphs live inside a <div>. Matches the ".idea" sample book. The
// paragraph LUT's body-direct counter stays 0 for these, so the forward mapper's
// paragraph-indexed path returns empty and callers fall back to progress-based mapping.
const std::string kWrappedChapter = R"(<?xml version="1.0" encoding="utf-8"?>
<html><body>
<div>
<h2>Chapter Heading</h2>
<p>First wrapped paragraph. Some filler content goes here.</p>
<p>Second wrapped paragraph. A bit longer to give the forward mapper room to land.</p>
<p>Third wrapped paragraph.</p>
<p>Fourth wrapped paragraph with enough text to carry a text-node offset test.</p>
</div>
</body></html>)";

// Paragraph with inline <span>/<i> children — exercises text-node counting semantics
// (text()[K] should increment when character data resumes after an inline element closes).
const std::string kInlineChapter = R"(<?xml version="1.0" encoding="utf-8"?>
<html><body>
<p>Start here <span>inline span</span> then back to body text <i>italic bit</i> final tail.</p>
</body></html>)";

bool startsWith(const std::string& haystack, const std::string& prefix) {
  return haystack.size() >= prefix.size() && haystack.compare(0, prefix.size(), prefix) == 0;
}

// Extract the codepoint offset N from a KOReader text-offset XPath suffix like
// ".../text()[K].N". Returns -1 if no suffix is present or the value isn't numeric.
int parseTextOffset(const std::string& xpath) {
  const size_t dot = xpath.rfind('.');
  if (dot == std::string::npos) return -1;
  // Guard against a "." that belongs to the DocFragment literal rather than a text offset.
  const size_t textMarker = xpath.rfind("/text()[");
  if (textMarker == std::string::npos || textMarker > dot) return -1;
  const std::string suffix = xpath.substr(dot + 1);
  if (suffix.empty()) return -1;
  for (const char c : suffix) {
    if (c < '0' || c > '9') return -1;
  }
  return std::atoi(suffix.c_str());
}

}  // namespace

// --- findXPathForParagraphFromBuffer --------------------------------------------------

void testParagraphOnlyFlatChapter() {
  std::printf("testParagraphOnlyFlatChapter...\n");
  // No progress hint => pure paragraph-start XPath, matching the original paragraph-LUT path.
  const auto xp = findXPathForParagraphFromBuffer(/*spineIndex=*/0, /*paragraphIndex=*/2, kFlatChapter);
  ASSERT_STR_EQ(xp, "/body/DocFragment[1]/body/p[2]");
  PASS();
}

void testParagraphOnlyPicksLastWhenIndexOutOfRange() {
  std::printf("testParagraphOnlyPicksLastWhenIndexOutOfRange...\n");
  // Current forward mapper stops at the Nth paragraph seen — if N exceeds the available
  // body-direct <p> count, no result is produced. This is the deterministic behavior callers
  // rely on when falling back to findXPathForProgress.
  const auto xp = findXPathForParagraphFromBuffer(/*spineIndex=*/0, /*paragraphIndex=*/99, kFlatChapter);
  ASSERT_TRUE(xp.empty());
  PASS();
}

void testParagraphOnlyWrappedChapterReturnsEmpty() {
  std::printf("testParagraphOnlyWrappedChapterReturnsEmpty...\n");
  // The paragraph LUT (and the matching forward-mapper count) only considers direct body
  // children. For wrapped chapters every <p> is nested inside a <div>, so no paragraph can
  // match. ProgressMapper relies on this to fall through to findXPathForProgress.
  const auto xp = findXPathForParagraphFromBuffer(/*spineIndex=*/0, /*paragraphIndex=*/2, kWrappedChapter);
  ASSERT_TRUE(xp.empty());
  PASS();
}

// --- Regression: 1.37 intra-paragraph precision restored -----------------------------

void testParagraphWithProgressEmitsTextOffset() {
  std::printf("testParagraphWithProgressEmitsTextOffset...\n");
  // Total visible bytes across the four flat paragraphs is ~176. p[1] ends near byte 27,
  // p[2] near byte 97. Targeting intra=0.4 (~70 bytes) lands mid-p[2] and must emit a
  // text()[1].N suffix — this is the precise regression guard vs. 1.38 (which would have
  // returned the paragraph-start XPath instead).
  const auto xp = findXPathForParagraphFromBuffer(/*spineIndex=*/0, /*paragraphIndex=*/2,
                                                  kFlatChapter, /*intraSpineProgress=*/0.4f);
  ASSERT_TRUE(startsWith(xp, "/body/DocFragment[1]/body/p[2]/text()[1]."));
  ASSERT_TRUE(parseTextOffset(xp) > 0);
  PASS();
}

void testParagraphStartWhenTargetBeforeParagraph() {
  std::printf("testParagraphStartWhenTargetBeforeParagraph...\n");
  // progress=0 puts the target byte at chapter start, before the requested p[3]. The mapper
  // should still emit a well-formed paragraph-start XPath rather than a weird suffix.
  const auto xp = findXPathForParagraphFromBuffer(/*spineIndex=*/0, /*paragraphIndex=*/3,
                                                  kFlatChapter, /*intraSpineProgress=*/0.0f);
  ASSERT_STR_EQ(xp, "/body/DocFragment[1]/body/p[3]");
  PASS();
}

void testParagraphWithProgressHandlesInlineChildren() {
  std::printf("testParagraphWithProgressHandlesInlineChildren...\n");
  // In kInlineChapter the <p> has three direct text nodes around two inline children.
  // With progress near the tail we should land in text()[3] and return a >0 char offset.
  const auto xp = findXPathForParagraphFromBuffer(/*spineIndex=*/0, /*paragraphIndex=*/1,
                                                  kInlineChapter, /*intraSpineProgress=*/0.95f);
  ASSERT_TRUE(startsWith(xp, "/body/DocFragment[1]/body/p[1]/text()[3]."));
  PASS();
}

// --- findXPathForProgressFromBuffer --------------------------------------------------

void testProgressFallbackFlatChapter() {
  std::printf("testProgressFallbackFlatChapter...\n");
  // progress-based mapping on flat EPUBs emits /p[N]/text()[1].N (direct body child level).
  const auto xp = findXPathForProgressFromBuffer(/*spineIndex=*/0, /*intraSpineProgress=*/0.1f, kFlatChapter);
  ASSERT_TRUE(startsWith(xp, "/body/DocFragment[1]/body/p[1]/text()[1]."));
  PASS();
}

void testProgressFallbackWrappedChapter() {
  std::printf("testProgressFallbackWrappedChapter...\n");
  // For wrapped chapters progress-based mapping cannot attach text()[N] (the <p> isn't a
  // direct body child), so we expect a paragraph-start XPath with the full wrapper ancestry.
  const auto xp = findXPathForProgressFromBuffer(/*spineIndex=*/0, /*intraSpineProgress=*/0.5f, kWrappedChapter);
  ASSERT_TRUE(startsWith(xp, "/body/DocFragment[1]/body/div[1]/p["));
  // No text-offset suffix must be appended when the target <p> is nested.
  ASSERT_EQ(parseTextOffset(xp), -1);
  PASS();
}

void testProgressAtZeroEmitsBaseXPath() {
  std::printf("testProgressAtZeroEmitsBaseXPath...\n");
  // progress=0 with non-empty chapter: should land at the first text byte of the first <p>.
  const auto xp = findXPathForProgressFromBuffer(/*spineIndex=*/0, /*intraSpineProgress=*/0.0f, kFlatChapter);
  ASSERT_TRUE(startsWith(xp, "/body/DocFragment[1]/body/p[1]"));
  PASS();
}

void testProgressEmptyChapterFallsBackToBase() {
  std::printf("testProgressEmptyChapterFallsBackToBase...\n");
  const std::string empty = "<?xml version=\"1.0\" encoding=\"utf-8\"?><html><body></body></html>";
  const auto xp = findXPathForProgressFromBuffer(/*spineIndex=*/7, /*intraSpineProgress=*/0.5f, empty);
  ASSERT_STR_EQ(xp, "/body/DocFragment[8]/body");
  PASS();
}

// --- Reverse mapper ------------------------------------------------------------------

void testReverseExactMatchFlatParagraph() {
  std::printf("testReverseExactMatchFlatParagraph...\n");
  float progress = -1.0f;
  bool exact = false;
  const bool ok = findProgressForXPathFromBuffer(/*spineIndex=*/0, "/body/DocFragment[1]/body/p[3]", kFlatChapter,
                                                 progress, exact);
  ASSERT_TRUE(ok);
  ASSERT_TRUE(exact);
  // p[3] starts after ~108 bytes of total ~171, i.e. ~0.63 intra-spine. Allow a generous
  // margin because visible-byte totals depend on the exact XHTML layout.
  ASSERT_TRUE(progress > 0.4f && progress < 0.85f);
  PASS();
}

void testReverseWrappedXPathRoundTrip() {
  std::printf("testReverseWrappedXPathRoundTrip...\n");
  // Forward-map a wrapped chapter and then reverse the result. Wrapped chapters lose the
  // character-offset suffix (no atBodyLevel text node under the <p>), so the round-trip
  // coarsens to paragraph-start granularity. The reconstructed progress should land
  // somewhere inside the matched paragraph's text range — verify by checking we didn't
  // snap all the way back to 0 and we didn't overshoot past the input.
  constexpr float kInputProgress = 0.5f;
  const auto xp = findXPathForProgressFromBuffer(/*spineIndex=*/0, kInputProgress, kWrappedChapter);
  ASSERT_TRUE(!xp.empty());

  float progress = -1.0f;
  bool exact = false;
  const bool ok = findProgressForXPathFromBuffer(/*spineIndex=*/0, xp, kWrappedChapter, progress, exact);
  ASSERT_TRUE(ok);
  // Paragraph-level snapping rounds DOWN to the paragraph start, so reconstructed progress
  // should be <= input. Allow up to one paragraph of slack (roughly 35% of this chapter).
  ASSERT_TRUE(progress <= kInputProgress + 0.02f);
  ASSERT_TRUE(progress >= kInputProgress - 0.4f);
  PASS();
}

void testReverseFlatTextOffsetRoundTrip() {
  std::printf("testReverseFlatTextOffsetRoundTrip...\n");
  // Forward emits /text()[1].N for flat chapters; reverse must parse the suffix and land
  // within a few percent of the input progress. This is the real-world upload→download loop.
  constexpr float kInputProgress = 0.35f;
  const auto xp = findXPathForProgressFromBuffer(/*spineIndex=*/0, kInputProgress, kFlatChapter);
  ASSERT_TRUE(startsWith(xp, "/body/DocFragment[1]/body/p["));
  ASSERT_TRUE(parseTextOffset(xp) >= 0);

  float progress = -1.0f;
  bool exact = false;
  const bool ok = findProgressForXPathFromBuffer(/*spineIndex=*/0, xp, kFlatChapter, progress, exact);
  ASSERT_TRUE(ok);
  // Exact text-offset matches should be tight: within 3% of input progress.
  ASSERT_CLOSE(progress, kInputProgress, 0.03f);
  PASS();
}

void testReverseParagraphWithProgressRoundTrip() {
  std::printf("testReverseParagraphWithProgressRoundTrip...\n");
  // This guards the precise regression: findXPathForParagraph + intraSpineProgress must
  // produce a text-offset XPath that reverse-maps back to approximately the original
  // progress. Previously (1.38) the forward result was just the paragraph start, so the
  // round-trip lost position information on every upload.
  constexpr float kInputProgress = 0.45f;
  const auto xp = findXPathForParagraphFromBuffer(/*spineIndex=*/0, /*paragraphIndex=*/2, kFlatChapter,
                                                  kInputProgress);
  ASSERT_TRUE(startsWith(xp, "/body/DocFragment[1]/body/p[2]/text()[1]."));

  float progress = -1.0f;
  bool exact = false;
  const bool ok = findProgressForXPathFromBuffer(/*spineIndex=*/0, xp, kFlatChapter, progress, exact);
  ASSERT_TRUE(ok);
  ASSERT_CLOSE(progress, kInputProgress, 0.03f);
  PASS();
}

// --- Run ------------------------------------------------------------------------------

int main() {
  testParagraphOnlyFlatChapter();
  testParagraphOnlyPicksLastWhenIndexOutOfRange();
  testParagraphOnlyWrappedChapterReturnsEmpty();

  testParagraphWithProgressEmitsTextOffset();
  testParagraphStartWhenTargetBeforeParagraph();
  testParagraphWithProgressHandlesInlineChildren();

  testProgressFallbackFlatChapter();
  testProgressFallbackWrappedChapter();
  testProgressAtZeroEmitsBaseXPath();
  testProgressEmptyChapterFallsBackToBase();

  testReverseExactMatchFlatParagraph();
  testReverseWrappedXPathRoundTrip();
  testReverseFlatTextOffsetRoundTrip();
  testReverseParagraphWithProgressRoundTrip();

  std::printf("\n%d passed, %d failed\n", testsPassed, testsFailed);
  return testsFailed == 0 ? 0 : 1;
}
