#pragma once

#include <Epub.h>

#include <memory>
#include <string>

namespace ChapterXPathIndexerInternal {

std::string findXPathForProgressInternal(const std::shared_ptr<Epub>& epub, int spineIndex, float intraSpineProgress);

// Find the full-ancestry XPath for the paragraphIndex-th direct-body-child <p> element.
// paragraphIndex is 1-based, matching the section paragraph LUT and KOReader XPath convention.
// seekHint is an optional XHTML byte offset to start scanning from (0 = scan from beginning).
// startParagraphCount is the number of body-child <p> elements already seen before seekHint
// (i.e. the paragraphIndex of the LUT entry at the seek page, minus 1). Ignored when seekHint=0.
// intraSpineProgress (0.0-1.0) optionally requests intra-paragraph character-offset precision:
// when >= 0.0, the emitted XPath is extended with /text()[K].cpOffset when the target visible-byte
// offset falls inside the target paragraph's direct text nodes. A negative value (or out-of-range)
// disables the refinement and the function emits only the paragraph-start XPath. When offset
// precision is requested the function forces a full parse from byte 0 (seekHint is ignored)
// because accurate visible-byte accounting requires seeing the chapter from the beginning.
// Returns empty string on failure; caller should fall back to findXPathForProgressInternal.
std::string findXPathForParagraphInternal(const std::shared_ptr<Epub>& epub, int spineIndex, uint16_t paragraphIndex,
                                          uint32_t seekHint = 0, uint16_t startParagraphCount = 0,
                                          float intraSpineProgress = -1.0f);

// Pure, buffer-based entry points for host-side testing. These mirror the production paths but
// take an in-memory XHTML string instead of decompressing via the EPUB/SD stack, letting tests
// exercise the parser/state machines without touching HalStorage or Epub.
// The intra-progress variant of the paragraph path re-counts total text bytes from the buffer
// itself, so each call is O(buffer).
std::string findXPathForParagraphFromBuffer(int spineIndex, uint16_t paragraphIndex, const std::string& xhtml,
                                            float intraSpineProgress = -1.0f);
std::string findXPathForProgressFromBuffer(int spineIndex, float intraSpineProgress, const std::string& xhtml);

}  // namespace ChapterXPathIndexerInternal
