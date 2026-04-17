#include "ParsedText.h"

#include <EpdFontData.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <vector>

#include "hyphenation/Hyphenator.h"

constexpr int MAX_COST = std::numeric_limits<int>::max();

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

// Returns the first rendered codepoint of a word (skipping leading soft hyphens).
uint32_t firstCodepoint(const std::string& word) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  while (true) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) return 0;
    if (cp != 0x00AD) return cp;  // skip soft hyphens
  }
}

// Returns the last codepoint of a word by scanning backward for the start of the last UTF-8 sequence.
uint32_t lastCodepoint(const std::string& word) {
  if (word.empty()) return 0;
  // UTF-8 continuation bytes start with 10xxxxxx; scan backward to find the leading byte.
  size_t i = word.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(word[i]) & 0xC0) == 0x80) {
    --i;
  }
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str() + i);
  return utf8NextCodepoint(&ptr);
}

bool containsSoftHyphen(const std::string& word) { return word.find(SOFT_HYPHEN_UTF8) != std::string::npos; }

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}

// Returns the advance width for a word while ignoring soft hyphen glyphs and optionally appending a visible hyphen.
// Uses advance width (sum of glyph advances + kerning) rather than bounding box width so that italic glyph overhangs
// don't inflate inter-word spacing.
uint16_t measureWordWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                          const EpdFontFamily::Style style, const bool appendHyphen = false) {
  if (word.size() == 1 && word[0] == ' ' && !appendHyphen) {
    return renderer.getSpaceWidth(fontId, style);
  }
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen) {
    return renderer.getTextAdvanceX(fontId, word.c_str(), style);
  }

  std::string sanitized = word;
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextAdvanceX(fontId, sanitized.c_str(), style);
}

std::string buildLinePreview(const std::vector<std::string>& words, const std::vector<bool>& continuesVec,
                             const size_t start, const size_t endExclusive, const size_t maxLen = 120) {
  // Build a readable line preview while preserving continuation semantics
  // (no synthetic spaces before attached tokens).
  std::string preview;
  for (size_t idx = start; idx < endExclusive; ++idx) {
    if (idx > start && idx < continuesVec.size() && !continuesVec[idx]) {
      preview.push_back(' ');
    }
    preview += words[idx];
    if (preview.size() >= maxLen) {
      preview.resize(maxLen);
      preview += "...";
      break;
    }
  }
  return preview;
}

}  // namespace

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle, const bool underline,
                         const bool attachToPrevious) {
  if (word.empty()) return;

  words.push_back(std::move(word));
  EpdFontFamily::Style combinedStyle = fontStyle;
  if (underline) {
    combinedStyle = static_cast<EpdFontFamily::Style>(combinedStyle | EpdFontFamily::UNDERLINE);
  }
  wordStyles.push_back(combinedStyle);
  wordContinues.push_back(attachToPrevious);
}

// Calculate the worst-line badness for a set of line breaks.
// Returns the maximum spare space per gap across all non-last lines.
static int evaluateBadness(const std::vector<size_t>& lineBreakIndices, const std::vector<uint16_t>& wordWidths,
                           const std::vector<bool>& continuesVec, const std::vector<std::string>& wordsRef,
                           const std::vector<EpdFontFamily::Style>& wordStylesRef, const GfxRenderer& renderer,
                           const int fontId, const int pageWidth) {
  int worstBadness = 0;
  const size_t lineCount = lineBreakIndices.size();

  for (size_t bi = 0; bi < lineCount; bi++) {
    const size_t lineStart = bi > 0 ? lineBreakIndices[bi - 1] : 0;
    const size_t lineEnd = lineBreakIndices[bi];
    const bool isLast = (bi == lineCount - 1);
    if (isLast) continue;  // last line is not justified

    int32_t contentFP = 0;
    int gapCount = 0;
    for (size_t wi = lineStart; wi < lineEnd; wi++) {
      contentFP += fp4::fromPixel(wordWidths[wi]);
      if (wi > lineStart && !continuesVec[wi]) {
        gapCount++;
        contentFP += renderer.getSpaceAdvanceFP(fontId, lastCodepoint(wordsRef[wi - 1]),
                                                firstCodepoint(wordsRef[wi]), wordStylesRef[wi - 1]);
      } else if (wi > lineStart && continuesVec[wi]) {
        contentFP += renderer.getKerningFP(fontId, lastCodepoint(wordsRef[wi - 1]),
                                           firstCodepoint(wordsRef[wi]), wordStylesRef[wi - 1]);
      }
    }
    const int spare = pageWidth - fp4::toPixel(contentFP);
    const int absSpare = spare >= 0 ? spare : -spare;
    const int badness = gapCount > 0 ? absSpare / gapCount : absSpare;
    if (badness > worstBadness) worstBadness = badness;
  }
  return worstBadness;
}

// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(
    const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
    const std::function<LineProcessResult(std::shared_ptr<TextBlock>, bool, bool)>& processLine,
    const bool includeLastLine) {
  if (words.empty()) {
    return;
  }

  // Apply fixed transforms before any per-line layout work.
  applyParagraphIndent();

  const int pageWidth = viewportWidth;
  auto wordWidths = calculateWordWidths(renderer, fontId);

  std::vector<size_t> lineBreakIndices;
  std::vector<bool> lineEndsWithHyphenatedWord;
  std::vector<int> splitPrefixWordIndexes;
  std::vector<bool> splitInsertedHyphen;
  int8_t paragraphTracking = 0;

  // Helper: compute line breaks via the hyphenating or non-hyphenating path.
  // Fills lineEndsWithHyphenatedWord/splitPrefixWordIndexes/splitInsertedHyphen consistently.
  auto computeLineBreaksWithHyphenationMeta = [&](std::vector<uint16_t>& ww, std::vector<bool>& wc,
                                                  std::vector<bool>& ends, std::vector<int>& prefixes,
                                                  std::vector<bool>& inserted) {
    std::vector<size_t> breaks;
    if (hyphenationEnabled) {
      breaks = computeHyphenatedLineBreaks(renderer, fontId, pageWidth, ww, wc, ends, prefixes, inserted);
    } else {
      breaks = computeLineBreaks(renderer, fontId, pageWidth, ww, wc);
      ends.assign(breaks.size(), false);
      prefixes.assign(breaks.size(), -1);
      inserted.assign(breaks.size(), false);
    }
    return breaks;
  };

  // For justified text, try paragraph-level tracking optimization.
  // Trial line-breaking passes mutate member vectors (words, wordStyles, wordContinues)
  // via hyphenation, so we save/restore around each trial and do one final definitive pass.
  if (blockStyle.alignment == CssTextAlign::Justify) {
    const auto savedWords = words;
    const auto savedStyles = wordStyles;
    const auto savedContinues = wordContinues;
    const auto savedWidths = wordWidths;

    // Count characters per word (pre-hyphenation, for tracking width adjustment)
    std::vector<int> charCounts(words.size());
    for (size_t i = 0; i < words.size(); i++) {
      int n = 0;
      const auto* p = reinterpret_cast<const unsigned char*>(words[i].c_str());
      while (*p) {
        utf8NextCodepoint(&p);
        n++;
      }
      charCounts[i] = n;
    }

    // Trial pass to evaluate badness. Hyphenation may expand member vectors in sync.
    {
      auto trialWidths = wordWidths;
      std::vector<bool> trialEnds;
      std::vector<int> trialPrefixes;
      std::vector<bool> trialInserted;
      auto trialBreaks =
          computeLineBreaksWithHyphenationMeta(trialWidths, wordContinues, trialEnds, trialPrefixes, trialInserted);

      if (trialBreaks.size() > 1) {
        const int naturalSpace = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
        const int currentBadness =
            evaluateBadness(trialBreaks, trialWidths, wordContinues, words, wordStyles, renderer, fontId, pageWidth);

        if (currentBadness > naturalSpace) {
          int bestBadness = currentBadness;
          // Trial range = half of cap (cap = naturalSpace_px/8 in FP4 = naturalSpace_px*2 FP4)
          const int8_t trialRange = static_cast<int8_t>(naturalSpace);

          for (int8_t tryTracking = -trialRange; tryTracking <= trialRange; tryTracking++) {
            if (tryTracking == 0) continue;

            // Restore pre-hyphenation state for this trial
            words = savedWords;
            wordStyles = savedStyles;
            wordContinues = savedContinues;

            auto adjustedWidths = savedWidths;
            for (size_t i = 0; i < adjustedWidths.size(); i++) {
              const int expansion = ((charCounts[i] - 1) * tryTracking + 8) >> 4;  // FP4 to pixels, rounded
              const int adjusted = static_cast<int>(adjustedWidths[i]) + expansion;
              adjustedWidths[i] = static_cast<uint16_t>(std::max(1, adjusted));
            }

            std::vector<bool> tryEnds;
            std::vector<int> tryPrefixes;
            std::vector<bool> tryInserted;
            auto tryBreaks =
                computeLineBreaksWithHyphenationMeta(adjustedWidths, wordContinues, tryEnds, tryPrefixes, tryInserted);

            const int tryBadness = evaluateBadness(tryBreaks, adjustedWidths, wordContinues, words, wordStyles,
                                                   renderer, fontId, pageWidth);

            if (tryBadness < bestBadness) {
              bestBadness = tryBadness;
              paragraphTracking = tryTracking;
            }
            if (bestBadness == 0) break;  // can't improve further
          }
        }
      }
    }

    // Final definitive pass — restore pre-hyphenation state and apply winning tracking
    words = savedWords;
    wordStyles = savedStyles;
    wordContinues = savedContinues;
    wordWidths = savedWidths;

    if (paragraphTracking != 0) {
      for (size_t i = 0; i < wordWidths.size(); i++) {
        const int expansion = ((charCounts[i] - 1) * paragraphTracking + 8) >> 4;
        const int adjusted = static_cast<int>(wordWidths[i]) + expansion;
        wordWidths[i] = static_cast<uint16_t>(std::max(1, adjusted));
      }
    }

    lineBreakIndices = computeLineBreaksWithHyphenationMeta(wordWidths, wordContinues, lineEndsWithHyphenatedWord,
                                                            splitPrefixWordIndexes, splitInsertedHyphen);
  } else {
    // Not justified — single pass, no tracking optimization
    lineBreakIndices = computeLineBreaksWithHyphenationMeta(wordWidths, wordContinues, lineEndsWithHyphenatedWord,
                                                            splitPrefixWordIndexes, splitInsertedHyphen);
  }

  size_t lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    const bool lineEndedWithHyphenation = i < lineEndsWithHyphenatedWord.size() ? lineEndsWithHyphenatedWord[i] : false;
    const auto result = extractLine(i, pageWidth, wordWidths, wordContinues, lineBreakIndices, processLine, renderer,
                                    fontId, lineEndedWithHyphenation, false, paragraphTracking);

    if (result == LineProcessResult::RetryWithoutHyphenation && lineEndedWithHyphenation) {
      const size_t lineStart = i > 0 ? lineBreakIndices[i - 1] : 0;
      const size_t lineEnd = i < lineBreakIndices.size() ? lineBreakIndices[i] : lineStart;
      const std::string firstAttemptPreview = buildLinePreview(words, wordContinues, lineStart, lineEnd);
      LOG_DBG("PTX", "Line %u requested rerender without hyphenation, first attempt: %s", static_cast<unsigned>(i),
              firstAttemptPreview.c_str());
      // Undo precomputed splits from this line onward so the retry starts from
      // clean, unsplit tokens and cannot inherit future hyphenation artifacts.
      std::set<int, std::greater<int>> splitIndexesToUndo;
      for (size_t lineIdx = i; lineIdx < splitPrefixWordIndexes.size(); ++lineIdx) {
        const int splitIndex = splitPrefixWordIndexes[lineIdx];
        if (splitIndex >= 0) {
          splitIndexesToUndo.insert(splitIndex);
        }
      }
      for (const int splitIndex : splitIndexesToUndo) {
        if (splitIndex < 0 || static_cast<size_t>(splitIndex + 1) >= words.size()) {
          continue;
        }
        bool removeInsertedHyphen = false;
        for (size_t lineIdx = i; lineIdx < splitPrefixWordIndexes.size(); ++lineIdx) {
          if (splitPrefixWordIndexes[lineIdx] == splitIndex && lineIdx < splitInsertedHyphen.size()) {
            removeInsertedHyphen = splitInsertedHyphen[lineIdx];
            break;
          }
        }

        std::string merged = words[splitIndex];
        if (removeInsertedHyphen && !merged.empty() && merged.back() == '-') {
          merged.pop_back();
        }
        merged += words[splitIndex + 1];
        words[splitIndex] = std::move(merged);
        words.erase(words.begin() + splitIndex + 1);
        wordStyles.erase(wordStyles.begin() + splitIndex + 1);
        wordContinues.erase(wordContinues.begin() + splitIndex + 1);
      }

      // Recompute widths after restoring unsplit words.
      wordWidths = calculateWordWidths(renderer, fontId);

      // Keep previous lines fixed; recompute only this specific line without hyphenation.
      // Suppression is intentionally line-local.
      const size_t retryBreak =
          computeSingleLineBreakNoHyphen(renderer, fontId, pageWidth, wordWidths, wordContinues, lineStart);

      lineBreakIndices.resize(i + 1);
      lineEndsWithHyphenatedWord.resize(i + 1);
      splitPrefixWordIndexes.resize(i + 1);
      splitInsertedHyphen.resize(i + 1);

      lineBreakIndices[i] = retryBreak;
      lineEndsWithHyphenatedWord[i] = false;
      splitPrefixWordIndexes[i] = -1;
      splitInsertedHyphen[i] = false;
      lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

      if (i < lineBreakIndices.size()) {
        const size_t retryLineStart = i > 0 ? lineBreakIndices[i - 1] : 0;
        const size_t retryLineEnd = i < lineBreakIndices.size() ? lineBreakIndices[i] : retryLineStart;
        const std::string retryPreview = buildLinePreview(words, wordContinues, retryLineStart, retryLineEnd);
        LOG_DBG("PTX", "Rerendering line %u with hyphenation suppressed, retry attempt: %s", static_cast<unsigned>(i),
                retryPreview.c_str());
        extractLine(i, pageWidth, wordWidths, wordContinues, lineBreakIndices, processLine, renderer, fontId, false,
                    true, paragraphTracking);

        // Resume regular hyphenation from the first word after the retried line.
        const size_t resumeIndex = lineBreakIndices[i];
        std::vector<bool> suffixLineEndsWithHyphenatedWord;
        std::vector<int> suffixSplitPrefixWordIndexes;
        std::vector<bool> suffixSplitInsertedHyphen;
        const auto hyphenatedSuffixBreaks = computeHyphenatedLineBreaksFromIndex(
            renderer, fontId, pageWidth, wordWidths, wordContinues, resumeIndex, suffixLineEndsWithHyphenatedWord,
            suffixSplitPrefixWordIndexes, suffixSplitInsertedHyphen);

        lineBreakIndices.insert(lineBreakIndices.end(), hyphenatedSuffixBreaks.begin(), hyphenatedSuffixBreaks.end());
        lineEndsWithHyphenatedWord.insert(lineEndsWithHyphenatedWord.end(), suffixLineEndsWithHyphenatedWord.begin(),
                                          suffixLineEndsWithHyphenatedWord.end());
        splitPrefixWordIndexes.insert(splitPrefixWordIndexes.end(), suffixSplitPrefixWordIndexes.begin(),
                                      suffixSplitPrefixWordIndexes.end());
        splitInsertedHyphen.insert(splitInsertedHyphen.end(), suffixSplitInsertedHyphen.begin(),
                                   suffixSplitInsertedHyphen.end());

        lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;
        LOG_DBG("PTX", "Resumed regular hyphenation after rerendered line %u", static_cast<unsigned>(i));
      }
    }
  }

  // Remove consumed words so size() reflects only remaining words
  if (lineCount > 0) {
    const size_t consumed = lineBreakIndices[lineCount - 1];
    words.erase(words.begin(), words.begin() + consumed);
    wordStyles.erase(wordStyles.begin(), wordStyles.begin() + consumed);
    wordContinues.erase(wordContinues.begin(), wordContinues.begin() + consumed);
  }
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(words.size());

  for (size_t i = 0; i < words.size(); ++i) {
    wordWidths.push_back(measureWordWidth(renderer, fontId, words[i], wordStyles[i]));
  }

  return wordWidths;
}

std::vector<size_t> ParsedText::computeLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                                  std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec) {
  if (words.empty()) {
    return {};
  }

  // Calculate first line indent (only for left/justified text).
  // Positive text-indent (paragraph indent) is suppressed when extraParagraphSpacing is on.
  // Negative text-indent (hanging indent, e.g. margin-left:3em; text-indent:-1em) always applies —
  // it is structural (positions the bullet/marker), not decorative.
  const int firstLineIndent =
      blockStyle.textIndentDefined && (blockStyle.textIndent < 0 || !extraParagraphSpacing) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  // Ensure any word that would overflow even as the first entry on a line is split using fallback hyphenation.
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    // First word needs to fit in reduced width if there's an indent
    const int effectiveWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;
    while (wordWidths[i] > effectiveWidth) {
      if (!hyphenateWordAtIndex(i, effectiveWidth, renderer, fontId, wordWidths, /*allowFallbackBreaks=*/true)) {
        break;
      }
    }
  }

  const size_t totalWordCount = words.size();

  // Pre-compute inter-word gaps once so the O(n²) DP inner loop avoids repeated
  // codepoint scanning and renderer calls for every (i,j) pair.
  // interWordGapsFP[j] = the spacing (12.4 FP) between words[j-1] and words[j] (0 for j==0).
  std::vector<int32_t> interWordGapsFP(totalWordCount, 0);
  for (size_t j = 1; j < totalWordCount; ++j) {
    if (!continuesVec[j]) {
      interWordGapsFP[j] = renderer.getSpaceAdvanceFP(fontId, lastCodepoint(words[j - 1]), firstCodepoint(words[j]),
                                                     wordStyles[j - 1]);
    } else {
      interWordGapsFP[j] =
          renderer.getKerningFP(fontId, lastCodepoint(words[j - 1]), firstCodepoint(words[j]), wordStyles[j - 1]);
    }
  }

  // DP table to store the minimum badness (cost) of lines starting at index i
  std::vector<int> dp(totalWordCount);
  // 'ans[i]' stores the index 'j' of the *last word* in the optimal line starting at 'i'
  std::vector<size_t> ans(totalWordCount);

  // Base Case
  dp[totalWordCount - 1] = 0;
  ans[totalWordCount - 1] = totalWordCount - 1;

  for (int i = totalWordCount - 2; i >= 0; --i) {
    int32_t currlenFP = 0;  // accumulate in 12.4 fixed-point
    dp[i] = MAX_COST;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;

    for (size_t j = i; j < totalWordCount; ++j) {
      const int32_t gapFP = (j > static_cast<size_t>(i)) ? interWordGapsFP[j] : 0;
      currlenFP += fp4::fromPixel(wordWidths[j]) + gapFP;

      const int currlen = fp4::toPixel(currlenFP);
      if (currlen > effectivePageWidth) {
        break;
      }

      // Cannot break after word j if the next word attaches to it (continuation group)
      if (j + 1 < totalWordCount && continuesVec[j + 1]) {
        continue;
      }

      int cost;
      if (j == totalWordCount - 1) {
        cost = 0;  // Last line
      } else {
        const int remainingSpace = effectivePageWidth - currlen;
        // Use long long for the square to prevent overflow
        const long long cost_ll = static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];

        if (cost_ll > MAX_COST) {
          cost = MAX_COST;
        } else {
          cost = static_cast<int>(cost_ll);
        }
      }

      if (cost < dp[i]) {
        dp[i] = cost;
        ans[i] = j;  // j is the index of the last word in this optimal line
      }
    }

    // Handle oversized word: if no valid configuration found, force single-word line
    // This prevents cascade failure where one oversized word breaks all preceding words
    if (dp[i] == MAX_COST) {
      ans[i] = i;  // Just this word on its own line
      // Inherit cost from next word to allow subsequent words to find valid configurations
      if (i + 1 < static_cast<int>(totalWordCount)) {
        dp[i] = dp[i + 1];
      } else {
        dp[i] = 0;
      }
    }
  }

  // Stores the index of the word that starts the next line (last_word_index + 1)
  std::vector<size_t> lineBreakIndices;
  size_t currentWordIndex = 0;

  while (currentWordIndex < totalWordCount) {
    size_t nextBreakIndex = ans[currentWordIndex] + 1;

    // Safety check: prevent infinite loop if nextBreakIndex doesn't advance
    if (nextBreakIndex <= currentWordIndex) {
      // Force advance by at least one word to avoid infinite loop
      nextBreakIndex = currentWordIndex + 1;
    }

    lineBreakIndices.push_back(nextBreakIndex);
    currentWordIndex = nextBreakIndex;
  }

  return lineBreakIndices;
}

size_t ParsedText::computeSingleLineBreakNoHyphen(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                                  const std::vector<uint16_t>& wordWidths,
                                                  const std::vector<bool>& continuesVec,
                                                  const size_t lineStartIndex) const {
  // One-line non-hyphenating breaker used by the page-boundary retry path.
  if (lineStartIndex >= wordWidths.size()) {
    return lineStartIndex;
  }

  const int firstLineIndent =
      lineStartIndex == 0 && blockStyle.textIndentDefined && (blockStyle.textIndent < 0 || !extraParagraphSpacing) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;
  const int effectivePageWidth = pageWidth - firstLineIndent;

  size_t currentIndex = lineStartIndex;
  int lineWidth = 0;

  while (currentIndex < wordWidths.size()) {
    const bool isFirstWord = currentIndex == lineStartIndex;
    int spacing = 0;
    if (!isFirstWord) {
      if (!continuesVec[currentIndex]) {
        spacing = renderer.getSpaceAdvance(fontId, lastCodepoint(words[currentIndex - 1]),
                                           firstCodepoint(words[currentIndex]), wordStyles[currentIndex - 1]);
      } else {
        spacing = renderer.getKerning(fontId, lastCodepoint(words[currentIndex - 1]),
                                      firstCodepoint(words[currentIndex]), wordStyles[currentIndex - 1]);
      }
    }

    const int candidateWidth = spacing + wordWidths[currentIndex];
    if (lineWidth + candidateWidth <= effectivePageWidth) {
      lineWidth += candidateWidth;
      ++currentIndex;
      continue;
    }

    if (currentIndex == lineStartIndex) {
      ++currentIndex;
    }
    break;
  }

  while (currentIndex > lineStartIndex + 1 && currentIndex < wordWidths.size() && continuesVec[currentIndex]) {
    --currentIndex;
  }

  return currentIndex;
}

void ParsedText::applyParagraphIndent() {
  if (extraParagraphSpacing || words.empty()) {
    return;
  }

  if (blockStyle.textIndentDefined) {
    // CSS text-indent is explicitly set (even if 0) - don't use fallback EmSpace
    // The actual indent positioning is handled in extractLine()
  } else if (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left) {
    // No CSS text-indent defined - use EmSpace fallback for visual indent
    words.front().insert(0, "\xe2\x80\x83");
  }
}

// Builds break indices while opportunistically splitting the word that would overflow the current line.
std::vector<size_t> ParsedText::computeHyphenatedLineBreaks(const GfxRenderer& renderer, const int fontId,
                                                            const int pageWidth, std::vector<uint16_t>& wordWidths,
                                                            std::vector<bool>& continuesVec,
                                                            std::vector<bool>& lineEndsWithHyphenatedWord,
                                                            std::vector<int>& splitPrefixWordIndexes,
                                                            std::vector<bool>& splitInsertedHyphen) {
  // Calculate first line indent (only for left/justified text).
  // Positive text-indent (paragraph indent) is suppressed when extraParagraphSpacing is on.
  // Negative text-indent (hanging indent, e.g. margin-left:3em; text-indent:-1em) always applies —
  // it is structural (positions the bullet/marker), not decorative.
  const int firstLineIndent =
      blockStyle.textIndentDefined && (blockStyle.textIndent < 0 || !extraParagraphSpacing) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  // Pre-compute inter-word gaps (12.4 FP) to avoid repeated codepoint scanning and renderer
  // calls in the inner loop. When hyphenateWordAtIndex inserts a new word, we insert
  // a placeholder gap (0) at that position to keep the vector in sync; the remainder
  // is always the first word on the next line so its spacing is never used.
  std::vector<int32_t> interWordGapsFP(wordWidths.size(), 0);
  for (size_t j = 1; j < wordWidths.size(); ++j) {
    if (!continuesVec[j]) {
      interWordGapsFP[j] = renderer.getSpaceAdvanceFP(fontId, lastCodepoint(words[j - 1]), firstCodepoint(words[j]),
                                                     wordStyles[j - 1]);
    } else {
      interWordGapsFP[j] =
          renderer.getKerningFP(fontId, lastCodepoint(words[j - 1]), firstCodepoint(words[j]), wordStyles[j - 1]);
    }
  }

  std::vector<size_t> lineBreakIndices;
  lineEndsWithHyphenatedWord.clear();
  splitPrefixWordIndexes.clear();
  splitInsertedHyphen.clear();
  size_t currentIndex = 0;
  bool isFirstLine = true;

  while (currentIndex < wordWidths.size()) {
    const size_t lineStart = currentIndex;
    int32_t lineWidthFP = 0;  // accumulate in 12.4 fixed-point for precision
    bool lineEndedWithHyphenation = false;
    int splitPrefixIndex = -1;
    bool splitNeedsInsertedHyphen = false;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = isFirstLine ? pageWidth - firstLineIndent : pageWidth;

    // Consume as many words as possible for current line, splitting when prefixes fit
    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      const int32_t spacingFP = isFirstWord ? 0 : interWordGapsFP[currentIndex];
      const int32_t candidateWidthFP = spacingFP + fp4::fromPixel(wordWidths[currentIndex]);

      // Word fits on current line (compare snapped FP total against page width)
      if (fp4::toPixel(lineWidthFP + candidateWidthFP) <= effectivePageWidth) {
        lineWidthFP += candidateWidthFP;
        ++currentIndex;
        continue;
      }

      // Word would overflow — try to split based on hyphenation points
      const int availableWidth = effectivePageWidth - fp4::toPixel(lineWidthFP + spacingFP);
      const bool allowFallbackBreaks = isFirstWord;  // Only for first word on line

      bool insertedHyphen = false;
      if (availableWidth > 0 && hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths,
                                                     allowFallbackBreaks, &insertedHyphen)) {
        // Keep interWordGapsFP in sync: insert placeholder for the new remainder word.
        // The remainder is always the first word on the next line so this slot is never read.
        interWordGapsFP.insert(interWordGapsFP.begin() + currentIndex + 1, 0);
        lineEndedWithHyphenation = true;
        splitPrefixIndex = static_cast<int>(currentIndex);
        splitNeedsInsertedHyphen = insertedHyphen;
        // Prefix now fits; append it to this line and move to next line
        lineWidthFP += spacingFP + fp4::fromPixel(wordWidths[currentIndex]);
        ++currentIndex;
        break;
      }

      // Could not split: force at least one word per line to avoid infinite loop
      if (currentIndex == lineStart) {
        lineWidthFP += candidateWidthFP;
        ++currentIndex;
      }
      break;
    }

    // Don't break before a continuation word (e.g., orphaned "?" after "question").
    // Backtrack to the start of the continuation group so the whole group moves to the next line.
    while (currentIndex > lineStart + 1 && currentIndex < wordWidths.size() && continuesVec[currentIndex]) {
      --currentIndex;
    }

    if (lineEndedWithHyphenation &&
        (splitPrefixIndex < static_cast<int>(lineStart) || splitPrefixIndex >= static_cast<int>(currentIndex))) {
      lineEndedWithHyphenation = false;
      splitPrefixIndex = -1;
      splitNeedsInsertedHyphen = false;
    }

    lineBreakIndices.push_back(currentIndex);
    lineEndsWithHyphenatedWord.push_back(lineEndedWithHyphenation);
    splitPrefixWordIndexes.push_back(splitPrefixIndex);
    splitInsertedHyphen.push_back(splitNeedsInsertedHyphen);
    isFirstLine = false;
  }

  return lineBreakIndices;
}

std::vector<size_t> ParsedText::computeHyphenatedLineBreaksFromIndex(
    const GfxRenderer& renderer, const int fontId, const int pageWidth, std::vector<uint16_t>& wordWidths,
    std::vector<bool>& continuesVec, const size_t startIndex, std::vector<bool>& lineEndsWithHyphenatedWord,
    std::vector<int>& splitPrefixWordIndexes, std::vector<bool>& splitInsertedHyphen) {
  // Same greedy hyphenating breaker as the full pass, but scoped to a suffix.
  if (startIndex >= wordWidths.size()) {
    lineEndsWithHyphenatedWord.clear();
    splitPrefixWordIndexes.clear();
    splitInsertedHyphen.clear();
    return {};
  }

  std::vector<int32_t> interWordGapsFP(wordWidths.size(), 0);
  for (size_t j = 1; j < wordWidths.size(); ++j) {
    if (!continuesVec[j]) {
      interWordGapsFP[j] = renderer.getSpaceAdvanceFP(fontId, lastCodepoint(words[j - 1]), firstCodepoint(words[j]),
                                                     wordStyles[j - 1]);
    } else {
      interWordGapsFP[j] =
          renderer.getKerningFP(fontId, lastCodepoint(words[j - 1]), firstCodepoint(words[j]), wordStyles[j - 1]);
    }
  }

  std::vector<size_t> lineBreakIndices;
  lineEndsWithHyphenatedWord.clear();
  splitPrefixWordIndexes.clear();
  splitInsertedHyphen.clear();

  size_t currentIndex = startIndex;
  while (currentIndex < wordWidths.size()) {
    const size_t lineStart = currentIndex;
    int32_t lineWidthFP = 0;  // 12.4 fixed-point
    bool lineEndedWithHyphenation = false;
    int splitPrefixIndex = -1;
    bool splitNeedsInsertedHyphen = false;

    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      const int32_t spacingFP = isFirstWord ? 0 : interWordGapsFP[currentIndex];
      const int32_t candidateWidthFP = spacingFP + fp4::fromPixel(wordWidths[currentIndex]);

      if (fp4::toPixel(lineWidthFP + candidateWidthFP) <= pageWidth) {
        lineWidthFP += candidateWidthFP;
        ++currentIndex;
        continue;
      }

      const int availableWidth = pageWidth - fp4::toPixel(lineWidthFP + spacingFP);
      const bool allowFallbackBreaks = isFirstWord;

      bool insertedHyphen = false;
      if (availableWidth > 0 && hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths,
                                                     allowFallbackBreaks, &insertedHyphen)) {
        interWordGapsFP.insert(interWordGapsFP.begin() + currentIndex + 1, 0);
        lineEndedWithHyphenation = true;
        splitPrefixIndex = static_cast<int>(currentIndex);
        splitNeedsInsertedHyphen = insertedHyphen;
        lineWidthFP += spacingFP + fp4::fromPixel(wordWidths[currentIndex]);
        ++currentIndex;
        break;
      }

      if (currentIndex == lineStart) {
        lineWidthFP += candidateWidthFP;
        ++currentIndex;
      }
      break;
    }

    while (currentIndex > lineStart + 1 && currentIndex < wordWidths.size() && continuesVec[currentIndex]) {
      --currentIndex;
    }

    if (lineEndedWithHyphenation &&
        (splitPrefixIndex < static_cast<int>(lineStart) || splitPrefixIndex >= static_cast<int>(currentIndex))) {
      lineEndedWithHyphenation = false;
      splitPrefixIndex = -1;
      splitNeedsInsertedHyphen = false;
    }

    lineBreakIndices.push_back(currentIndex);
    lineEndsWithHyphenatedWord.push_back(lineEndedWithHyphenation);
    splitPrefixWordIndexes.push_back(splitPrefixIndex);
    splitInsertedHyphen.push_back(splitNeedsInsertedHyphen);
  }

  return lineBreakIndices;
}

// Splits words[wordIndex] into prefix (adding a hyphen only when needed) and remainder when a legal breakpoint fits the
// available width.
bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, std::vector<uint16_t>& wordWidths,
                                      const bool allowFallbackBreaks, bool* outInsertedHyphen) {
  // Guard against invalid indices or zero available width before attempting to split.
  if (availableWidth <= 0 || wordIndex >= words.size()) {
    return false;
  }

  const std::string& word = words[wordIndex];
  const auto style = wordStyles[wordIndex];

  // Collect candidate breakpoints (byte offsets and hyphen requirements).
  auto breakInfos = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
  int chosenWidth = -1;
  bool chosenNeedsHyphen = true;

  // Iterate over each legal breakpoint and retain the widest prefix that still fits.
  for (const auto& info : breakInfos) {
    const size_t offset = info.byteOffset;
    if (offset == 0 || offset >= word.size()) {
      continue;
    }

    const bool needsHyphen = info.requiresInsertedHyphen;
    const int prefixWidth = measureWordWidth(renderer, fontId, word.substr(0, offset), style, needsHyphen);
    if (prefixWidth > availableWidth || prefixWidth <= chosenWidth) {
      continue;  // Skip if too wide or not an improvement
    }

    chosenWidth = prefixWidth;
    chosenOffset = offset;
    chosenNeedsHyphen = needsHyphen;
  }

  if (chosenWidth < 0) {
    // No hyphenation point produced a prefix that fits in the remaining space.
    return false;
  }

  // Split the word at the selected breakpoint and append a hyphen if required.
  std::string remainder = word.substr(chosenOffset);
  words[wordIndex].resize(chosenOffset);
  if (chosenNeedsHyphen) {
    words[wordIndex].push_back('-');
  }

  // Insert the remainder word (with matching style and continuation flag) directly after the prefix.
  words.insert(words.begin() + wordIndex + 1, remainder);
  wordStyles.insert(wordStyles.begin() + wordIndex + 1, style);

  // Continuation flag handling after splitting a word into prefix + remainder.
  //
  // The prefix keeps the original word's continuation flag so that no-break-space groups
  // stay linked. The remainder always gets continues=false because it starts on the next
  // line and is not attached to the prefix.
  //
  // Example: "200&#xA0;Quadratkilometer" produces tokens:
  //   [0] "200"               continues=false
  //   [1] " "                 continues=true
  //   [2] "Quadratkilometer"  continues=true   <-- the word being split
  //
  // After splitting "Quadratkilometer" at "Quadrat-" / "kilometer":
  //   [0] "200"         continues=false
  //   [1] " "           continues=true
  //   [2] "Quadrat-"    continues=true   (KEPT — still attached to the no-break group)
  //   [3] "kilometer"   continues=false  (NEW — starts fresh on the next line)
  //
  // This lets the backtracking loop keep the entire prefix group ("200 Quadrat-") on one
  // line, while "kilometer" moves to the next line.
  // wordContinues[wordIndex] is intentionally left unchanged — the prefix keeps its original attachment.
  wordContinues.insert(wordContinues.begin() + wordIndex + 1, false);

  // Update cached widths to reflect the new prefix/remainder pairing.
  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);
  const uint16_t remainderWidth = measureWordWidth(renderer, fontId, remainder, style);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);
  if (outInsertedHyphen) {
    *outInsertedHyphen = chosenNeedsHyphen;
  }
  return true;
}

ParsedText::LineProcessResult ParsedText::extractLine(
    const size_t breakIndex, const int pageWidth, const std::vector<uint16_t>& wordWidths,
    const std::vector<bool>& continuesVec, const std::vector<size_t>& lineBreakIndices,
    const std::function<LineProcessResult(std::shared_ptr<TextBlock>, bool, bool)>& processLine,
    const GfxRenderer& renderer, const int fontId, const bool lineEndsWithHyphenatedWord,
    const bool suppressHyphenationRetry, const int8_t paragraphTracking) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  // Calculate first line indent (only for left/justified text).
  // Positive text-indent (paragraph indent) is suppressed when extraParagraphSpacing is on.
  // Negative text-indent (hanging indent, e.g. margin-left:3em; text-indent:-1em) always applies —
  // it is structural (positions the bullet/marker), not decorative.
  const bool isFirstLine = breakIndex == 0;
  const int firstLineIndent =
      isFirstLine && blockStyle.textIndentDefined && (blockStyle.textIndent < 0 || !extraParagraphSpacing) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  // Calculate total line content width in fixed-point, count actual word gaps.
  int32_t lineContentFP = 0;
  size_t actualGapCount = 0;

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineContentFP += fp4::fromPixel(wordWidths[lastBreakAt + wordIdx]);
    if (wordIdx > 0 && !continuesVec[lastBreakAt + wordIdx]) {
      actualGapCount++;
      lineContentFP +=
          renderer.getSpaceAdvanceFP(fontId, lastCodepoint(words[lastBreakAt + wordIdx - 1]),
                                     firstCodepoint(words[lastBreakAt + wordIdx]),
                                     wordStyles[lastBreakAt + wordIdx - 1]);
    } else if (wordIdx > 0 && continuesVec[lastBreakAt + wordIdx]) {
      lineContentFP +=
          renderer.getKerningFP(fontId, lastCodepoint(words[lastBreakAt + wordIdx - 1]),
                                firstCodepoint(words[lastBreakAt + wordIdx]),
                                wordStyles[lastBreakAt + wordIdx - 1]);
    }
  }

  // Calculate spacing (account for indent reducing effective page width on first line)
  const int effectivePageWidth = pageWidth - firstLineIndent;
  // A line is only truly last when it consumes all paragraph words.
  // During single-line retry we may temporarily pass a truncated break vector,
  // so relying only on breakIndex would incorrectly disable justification.
  const bool isLastLine = lineBreak == words.size();
  const int lineContentPx = fp4::toPixel(lineContentFP);

  // For justified text, distribute remaining space across word gaps and optionally
  // into letter-spacing (tracking). Gaps absorb up to 50% extra beyond natural width;
  // excess goes into per-character tracking for more even appearance.
  const int spareSpace = effectivePageWidth - lineContentPx;
  const bool justify = blockStyle.alignment == CssTextAlign::Justify && !isLastLine && actualGapCount >= 1;

  int justifyExtraFP = 0;     // per-gap extra in 12.4 FP
  int justifyRemainderFP = 0; // leftover distributed to first N gaps in 12.4 FP
  int8_t trackingFP = paragraphTracking;  // start with paragraph-level tracking, add per-line overflow

  if (justify && spareSpace > 0) {
    const int32_t spareSpaceFP = fp4::fromPixel(spareSpace);
    const int naturalSpace = renderer.getSpaceWidth(fontId, wordStyles[lastBreakAt]);
    const int32_t maxGapExtraFP = fp4::fromPixel(naturalSpace) / 2;  // cap at 50% of natural space
    const int32_t maxGapAbsorptionFP = maxGapExtraFP * static_cast<int32_t>(actualGapCount);

    if (spareSpaceFP <= maxGapAbsorptionFP) {
      // All spare fits in gaps — no tracking needed
      justifyExtraFP = spareSpaceFP / static_cast<int32_t>(actualGapCount);
      justifyRemainderFP = spareSpaceFP - justifyExtraFP * static_cast<int32_t>(actualGapCount);
    } else {
      // Gaps absorb max, remainder goes to tracking
      justifyExtraFP = maxGapExtraFP;
      const int32_t trackingSpaceFP = spareSpaceFP - maxGapAbsorptionFP;

      // Count total characters for tracking distribution
      int totalChars = 0;
      for (size_t wi = 0; wi < lineWordCount; wi++) {
        const auto* p = reinterpret_cast<const unsigned char*>(words[lastBreakAt + wi].c_str());
        while (*p) { utf8NextCodepoint(&p); totalChars++; }
      }
      // drawText applies tracking (Ni - 1) times per word (between chars, not after last).
      // Total inter-character slots = totalChars - lineWordCount (one less per word).
      const int trackSlots = totalChars - static_cast<int>(lineWordCount);
      if (trackSlots > 0) {
        int32_t trackPerCharFP = trackingSpaceFP / trackSlots;
        // Cap = naturalSpaceWidth / 8 in FP4 = naturalSpace_px * 2 FP4 (scales with font size)
        const int32_t MAX_TRACKING_FP = static_cast<int32_t>(naturalSpace) * 2;
        int32_t totalTrackingFP = static_cast<int32_t>(paragraphTracking) + trackPerCharFP;
        if (totalTrackingFP > MAX_TRACKING_FP) totalTrackingFP = MAX_TRACKING_FP;
        // No negative-direction floor needed: per-line tracking is only added here when
        // spareSpace > 0, so trackPerCharFP >= 0 and totalTrackingFP >= paragraphTracking.
        // Negative paragraphTracking is already bounded by trialRange (±naturalSpace FP4).
        trackingFP = static_cast<int8_t>(totalTrackingFP);
        // Any space not absorbed by per-line tracking goes back to gaps
        const int32_t perLineAbsorbed = (totalTrackingFP - paragraphTracking) * trackSlots;
        const int32_t unabsorbed = trackingSpaceFP - perLineAbsorbed;
        justifyExtraFP += unabsorbed / static_cast<int32_t>(actualGapCount);
        justifyRemainderFP = unabsorbed - (unabsorbed / static_cast<int32_t>(actualGapCount)) * static_cast<int32_t>(actualGapCount);
      }
    }
  }

  // Calculate initial x position using FP accumulation
  int32_t xposFP = fp4::fromPixel(firstLineIndent);
  if (blockStyle.alignment == CssTextAlign::Right) {
    xposFP = fp4::fromPixel(effectivePageWidth - lineContentPx);
  } else if (blockStyle.alignment == CssTextAlign::Center) {
    xposFP = fp4::fromPixel((effectivePageWidth - lineContentPx) / 2);
  }

  // Pre-calculate X positions for words using FP accumulation.
  // When tracking is active, each word renders wider — account for expansion.
  std::vector<int16_t> lineXPos;
  lineXPos.reserve(lineWordCount);
  int32_t gapRemainderBudget = justifyRemainderFP;

  // Helper: count UTF-8 codepoints in a word
  auto countChars = [](const std::string& w) -> int {
    int n = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(w.c_str());
    while (*p) { utf8NextCodepoint(&p); n++; }
    return n;
  };

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineXPos.push_back(static_cast<int16_t>(fp4::toPixel(xposFP)));

    // Position expansion: only the per-line tracking delta beyond paragraph tracking,
    // since paragraph tracking is already baked into the expanded wordWidths.
    int32_t trackExpansionFP = 0;
    const int8_t deltaTrackingFP = trackingFP - paragraphTracking;
    if (deltaTrackingFP != 0) {
      const int charCount = countChars(words[lastBreakAt + wordIdx]);
      if (charCount > 1) {
        trackExpansionFP = static_cast<int32_t>(deltaTrackingFP) * (charCount - 1);
      }
    }

    const bool nextIsContinuation = wordIdx + 1 < lineWordCount && continuesVec[lastBreakAt + wordIdx + 1];
    if (nextIsContinuation) {
      xposFP += fp4::fromPixel(wordWidths[lastBreakAt + wordIdx]) + trackExpansionFP;
      xposFP += renderer.getKerningFP(fontId, lastCodepoint(words[lastBreakAt + wordIdx]),
                                       firstCodepoint(words[lastBreakAt + wordIdx + 1]),
                                       wordStyles[lastBreakAt + wordIdx]);
    } else {
      xposFP += fp4::fromPixel(wordWidths[lastBreakAt + wordIdx]) + trackExpansionFP;
      if (wordIdx + 1 < lineWordCount) {
        xposFP += renderer.getSpaceAdvanceFP(fontId, lastCodepoint(words[lastBreakAt + wordIdx]),
                                              firstCodepoint(words[lastBreakAt + wordIdx + 1]),
                                              wordStyles[lastBreakAt + wordIdx]);
      }
      if (justify) {
        xposFP += justifyExtraFP;
        if (gapRemainderBudget > 0) {
          // Distribute remainder one FP unit at a time
          xposFP += 1;
          gapRemainderBudget -= 1;
        }
      }
    }
  }

  // Copy line words; keep source intact so retry paths can safely inspect/merge tokens.
  std::vector<std::string> lineWords(words.begin() + lastBreakAt, words.begin() + lineBreak);
  std::vector<EpdFontFamily::Style> lineWordStyles(wordStyles.begin() + lastBreakAt, wordStyles.begin() + lineBreak);

  for (auto& word : lineWords) {
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
  }

  // Build per-word tracking vector (only when tracking is active)
  std::vector<int8_t> lineTracking;
  if (trackingFP != 0) {
    lineTracking.resize(lineWordCount, trackingFP);
  }

  return processLine(std::make_shared<TextBlock>(std::move(lineWords), std::move(lineXPos), std::move(lineWordStyles),
                                                  blockStyle, std::move(lineTracking)),
                     lineEndsWithHyphenatedWord, suppressHyphenationRetry);
}
