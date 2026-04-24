#include "ChapterXPathForwardMapper.h"

#include <HalStorage.h>
#include <Logging.h>
#include <expat.h>

#include <algorithm>
#include <string>
#include <unordered_map>

#include "ChapterXPathIndexerInternal.h"
#include "ChapterXPathIndexerState.h"

namespace ChapterXPathIndexerInternal {

namespace {

// Forward mapper: translate intra-spine progress to a KOReader-compatible XPath.
// Strategy:
// 1) Count total visible text bytes in chapter.
// 2) Stream parse again and stop when target byte offset is reached.
// 3) Emit either an element path or /text()[N].M when at body text-node level.

struct ForwardState : StackState {
  int spineIndex;
  size_t targetOffset;
  std::string result;
  bool found = false;
  XML_Parser parser = nullptr;

  int bodyTextNodeCount = 0;
  size_t codepointsInBodyTextNode = 0;
  bool inBodyTextNode = false;

  ForwardState(const int spineIndex, const size_t targetOffset) : spineIndex(spineIndex), targetOffset(targetOffset) {}

  void onStartElement(const XML_Char* rawName) {
    inBodyTextNode = false;
    pushElement(rawName);
  }

  void onEndElement() {
    inBodyTextNode = false;
    popElement();
  }

  void onCharData(const XML_Char* text, const int len) {
    if (shouldSkipText(len) || found) {
      return;
    }

    const bool atBodyLevel = bodyIdx() + 1 == static_cast<int>(stack.size());
    if (atBodyLevel && !inBodyTextNode) {
      inBodyTextNode = true;
      bodyTextNodeCount++;
      codepointsInBodyTextNode = 0;
    }

    if (isWhitespaceOnly(text, len)) {
      if (atBodyLevel) {
        codepointsInBodyTextNode += countUtf8Codepoints(text, len);
      }
      return;
    }

    const size_t visible = countVisibleBytes(text, len);
    if (totalTextBytes + visible >= targetOffset) {
      if (atBodyLevel && bodyTextNodeCount > 0) {
        // KOReader/crengine text-point semantics use codepoint offsets.
        const size_t targetVisibleByteInChunk = targetOffset - totalTextBytes;
        const size_t cpInChunk = codepointAtVisibleByte(text, len, targetVisibleByteInChunk);
        const size_t charOff = codepointsInBodyTextNode + cpInChunk;
        result =
            currentXPath(spineIndex) + "/text()[" + std::to_string(bodyTextNodeCount) + "]." + std::to_string(charOff);
      } else {
        result = currentXPath(spineIndex);
      }
      found = true;
      if (parser) {
        XML_StopParser(parser, XML_FALSE);
      }
      return;
    }

    totalTextBytes += visible;
    if (atBodyLevel) {
      codepointsInBodyTextNode += countUtf8Codepoints(text, len);
    }
  }
};

std::string makeSpineCacheKey(const std::shared_ptr<Epub>& epub, const int spineIndex) {
  if (!epub || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return "";
  }
  const auto spineItem = epub->getSpineItem(spineIndex);
  return epub->getCachePath() + "|" + std::to_string(spineIndex) + "|" + spineItem.href;
}

size_t getTotalTextBytesCached(const std::shared_ptr<Epub>& epub, const int spineIndex, const std::string& tmpPath) {
  static std::unordered_map<std::string, size_t> sTotalBytesBySpine;
  static std::string sCachedBookPath;

  const std::string currentBookPath = epub ? epub->getCachePath() : std::string();
  if (currentBookPath != sCachedBookPath) {
    sTotalBytesBySpine.clear();
    sCachedBookPath = currentBookPath;
  }

  const std::string key = makeSpineCacheKey(epub, spineIndex);
  if (!key.empty()) {
    const auto it = sTotalBytesBySpine.find(key);
    if (it != sTotalBytesBySpine.end()) {
      return it->second;
    }
  }

  const size_t totalTextBytes = countTotalTextBytes(tmpPath);
  if (!key.empty()) {
    sTotalBytesBySpine[key] = totalTextBytes;
  }
  return totalTextBytes;
}

}  // namespace

// Paragraph-targeted forward mapper.
// Counts direct-body-child <p> elements (matching ChapterHtmlSlimParser's xpathBodyDepth guard)
// and stops at the Nth one, emitting its full-ancestry XPath.  When the caller supplies an
// intraSpineProgress hint the mapper keeps parsing inside the matched <p> and appends a
// /text()[K].cpOffset suffix pointing at the exact codepoint offset — this restores KOReader's
// pre-1.38 upload precision for flat chapters without losing structural correctness for
// wrapped ones. The seek hint avoids scanning from byte 0 for the paragraph-only lookup, but
// is ignored when intra-paragraph precision is requested because accurate visible-byte
// accounting requires seeing the chapter from the beginning.
namespace {

struct ParagraphState : StackState {
  int spineIndex;
  uint16_t targetParagraph;  // 1-based
  uint16_t paragraphCount = 0;
  std::string result;
  XML_Parser parser = nullptr;
  // When parsing from a seek offset, the DOM context (html/body ancestors) is missing from
  // the parser's perspective.  partialParse=true relaxes the bodyIdx() check and instead
  // counts any <p> at depth 0 relative to the first element seen (a heuristic that works
  // because we know we're already inside <body> in the source document).
  bool partialParse = false;
  int partialBaseDepth = -1;  // stack depth when the first element is seen in partial mode

  // Intra-paragraph precision (only active when wantOffset=true; requires full parse).
  bool wantOffset = false;
  size_t targetByteOffset = 0;   // visible-text byte offset for the refinement target
  int targetPStackDepth = -1;    // stack depth of the matched <p>; -1 until it is entered
  std::string targetPXPath;      // cached full-ancestry XPath of the matched <p>
  int pTextNodeCount = 0;        // 1-based index of the current direct text-node child of <p>
  size_t codepointsInPText = 0;  // codepoints seen in the current text node
  bool inPTextNode = false;

  ParagraphState(const int spineIndex, const uint16_t targetParagraph, const uint16_t startParagraphCount,
                 const bool partialParse, const bool wantOffset, const size_t targetByteOffset)
      : spineIndex(spineIndex),
        targetParagraph(targetParagraph),
        paragraphCount(startParagraphCount),
        partialParse(partialParse),
        wantOffset(wantOffset),
        targetByteOffset(targetByteOffset) {}

  bool insideTargetP() const { return targetPStackDepth >= 0; }

  bool atTargetPDirectTextLevel() const {
    return insideTargetP() && static_cast<int>(stack.size()) == targetPStackDepth;
  }
};

void XMLCALL paragraphStartCb(void* ud, const XML_Char* rawName, const XML_Char**) {
  auto* s = static_cast<ParagraphState*>(ud);
  s->pushElement(rawName);

  // Any element boundary ends the current text node; a later text chunk will open a new one.
  s->inPTextNode = false;

  if (s->stack.empty() || s->stack.back().tag != "p") {
    return;
  }

  // Already locked onto a target <p> and still parsing inside it — any nested <p> (rare but
  // possible) is ignored. The outer-<p> boundary is what matters for our offset tracking.
  if (s->insideTargetP()) {
    return;
  }

  // If we already produced a final text-level result in a previous target-<p> match, bail.
  if (!s->result.empty() && !s->wantOffset) {
    return;
  }

  bool isDirectBodyChild = false;
  if (s->partialParse) {
    // In partial mode the DOM context (html/body ancestors) is absent from the parser.
    // We record the stack depth of the first element encountered as the body-equivalent
    // depth; direct body children are one level deeper.  This only works for flat EPUBs
    // where paragraphs are direct children of <body> — for wrapped chapters the partial
    // parse will find nothing and the caller retries from byte 0 with full context.
    if (s->partialBaseDepth < 0) {
      s->partialBaseDepth = static_cast<int>(s->stack.size()) - 1;
    }
    isDirectBodyChild = (static_cast<int>(s->stack.size()) - 1 == s->partialBaseDepth);
  } else {
    const int bi = s->bodyIdx();
    isDirectBodyChild = (bi >= 0 && static_cast<int>(s->stack.size()) == bi + 2);
  }

  if (!isDirectBodyChild) {
    return;
  }

  s->paragraphCount++;
  if (s->paragraphCount < s->targetParagraph) {
    return;
  }

  // Matched the target <p>. In no-offset mode we're done; otherwise keep parsing to drill into
  // its text nodes and compute the precise codepoint offset.
  s->targetPXPath = s->currentXPath(s->spineIndex);
  if (!s->wantOffset) {
    s->result = s->targetPXPath;
    if (s->parser) {
      XML_StopParser(s->parser, XML_FALSE);
    }
    return;
  }

  s->targetPStackDepth = static_cast<int>(s->stack.size());
  s->pTextNodeCount = 0;
  s->codepointsInPText = 0;
  s->inPTextNode = false;

  // If the target byte was already surpassed before this paragraph started (common when the
  // requested intra-spine progress rounds down to a position before the paragraph we chose via
  // the LUT), we have no way to refine further — emit paragraph-start right away so callers
  // still see a well-formed XPath. Same for zero-offset targets.
  if (s->totalTextBytes >= s->targetByteOffset) {
    s->result = s->targetPXPath;
    if (s->parser) {
      XML_StopParser(s->parser, XML_FALSE);
    }
  }
}

void XMLCALL paragraphEndCb(void* ud, const XML_Char*) {
  auto* s = static_cast<ParagraphState*>(ud);
  s->inPTextNode = false;

  const bool closingTargetP = s->insideTargetP() && static_cast<int>(s->stack.size()) == s->targetPStackDepth &&
                              !s->stack.empty() && s->stack.back().tag == "p";

  s->popElement();

  // If we leave the target <p> without having produced a refined text-level XPath (e.g. the
  // target byte was past the paragraph content, or the paragraph had no direct text nodes),
  // fall back to the paragraph-start XPath we captured on entry.
  if (closingTargetP && s->wantOffset && s->result.empty()) {
    s->result = s->targetPXPath;
    s->targetPStackDepth = -1;
    if (s->parser) {
      XML_StopParser(s->parser, XML_FALSE);
    }
  }
}

void XMLCALL paragraphCharCb(void* ud, const XML_Char* text, const int len) {
  auto* s = static_cast<ParagraphState*>(ud);
  if (len <= 0 || s->shouldSkipText(len)) {
    return;
  }

  const bool insideTargetPText = s->atTargetPDirectTextLevel() && s->result.empty();
  // Track text-node boundaries within the target <p> so we can emit /text()[K].N correctly.
  // A new text node begins whenever we see character data at the direct-child level and we
  // are not already inside one (element starts/ends reset inPTextNode).
  if (insideTargetPText && !s->inPTextNode) {
    s->inPTextNode = true;
    s->pTextNodeCount++;
    s->codepointsInPText = 0;
  }

  const bool whitespace = isWhitespaceOnly(text, len);

  // Advance accumulators. Whitespace doesn't count toward visible bytes but DOES count toward
  // codepoint offsets within a text node, matching the semantics of ForwardState.
  if (!whitespace) {
    const size_t visible = countVisibleBytes(text, len);

    // Refinement: if we're inside the target paragraph's direct text and the target byte falls
    // within this chunk, emit /text()[K].cpOffset and stop.
    if (insideTargetPText && s->wantOffset && s->result.empty() && s->totalTextBytes + visible >= s->targetByteOffset) {
      const size_t targetVisibleByteInChunk =
          (s->targetByteOffset > s->totalTextBytes) ? (s->targetByteOffset - s->totalTextBytes) : 0;
      const size_t cpInChunk = codepointAtVisibleByte(text, len, targetVisibleByteInChunk);
      const size_t charOff = s->codepointsInPText + cpInChunk;
      s->result = s->targetPXPath + "/text()[" + std::to_string(s->pTextNodeCount) + "]." + std::to_string(charOff);
      if (s->parser) {
        XML_StopParser(s->parser, XML_FALSE);
      }
      return;
    }

    s->totalTextBytes += visible;
  }

  if (insideTargetPText) {
    s->codepointsInPText += countUtf8Codepoints(text, len);
  }
}

}  // namespace

std::string findXPathForParagraphInternal(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                          const uint16_t paragraphIndex, const uint32_t seekHint,
                                          const uint16_t startParagraphCount, const float intraSpineProgress) {
  if (!epub || paragraphIndex == 0) {
    return "";
  }

  const std::string tmpPath = decompressToTempFile(epub, spineIndex);
  if (tmpPath.empty()) {
    return "";
  }

  // Intra-paragraph refinement needs accurate visible-byte accounting from byte 0, so it forces
  // a full parse even when the caller supplied a seek hint.
  const bool wantOffset = intraSpineProgress >= 0.0f;
  size_t targetByteOffset = 0;
  if (wantOffset) {
    const size_t totalTextBytes = getTotalTextBytesCached(epub, spineIndex, tmpPath);
    const float clamped = std::max(0.0f, std::min(1.0f, intraSpineProgress));
    targetByteOffset = static_cast<size_t>(clamped * static_cast<float>(totalTextBytes));
  }

  const bool partialParse = !wantOffset && seekHint > 0;
  ParagraphState state(spineIndex, paragraphIndex, partialParse ? startParagraphCount : 0, partialParse, wantOffset,
                       targetByteOffset);
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    Storage.remove(tmpPath.c_str());
    return "";
  }

  state.parser = parser;
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, paragraphStartCb, paragraphEndCb);
  XML_SetCharacterDataHandler(parser, paragraphCharCb);
  XML_SetDefaultHandlerExpand(parser, parserDefaultCb<ParagraphState>);

  if (partialParse) {
    // Use seek hint from section LUT if available — avoids scanning the whole chapter.
    // If the partial parse misses the target (e.g. the hint overshot), retry from byte 0.
    runParseFromOffset(parser, tmpPath, seekHint);
  } else {
    runParse(parser, tmpPath);
  }

  if (state.result.empty() && partialParse) {
    // Partial parse missed — reset and retry from beginning with full-document context.
    XML_ParserFree(parser);
    parser = XML_ParserCreate(nullptr);
    if (!parser) {
      LOG_ERR("KOX", "XML_ParserCreate failed on retry: spine=%d p[%u] tmp=%s", spineIndex, paragraphIndex,
              tmpPath.c_str());
    } else {
      ParagraphState fullState(spineIndex, paragraphIndex, 0, false, wantOffset, targetByteOffset);
      fullState.parser = parser;
      XML_SetUserData(parser, &fullState);
      XML_SetElementHandler(parser, paragraphStartCb, paragraphEndCb);
      XML_SetCharacterDataHandler(parser, paragraphCharCb);
      XML_SetDefaultHandlerExpand(parser, parserDefaultCb<ParagraphState>);
      runParse(parser, tmpPath);
      state.result = fullState.result;
    }
  }

  XML_ParserFree(parser);
  Storage.remove(tmpPath.c_str());

  LOG_DBG("KOX", "Paragraph: spine=%d p[%u] seekHint=%u intra=%.3f -> %s", spineIndex, paragraphIndex, seekHint,
          intraSpineProgress, state.result.empty() ? "(not found)" : state.result.c_str());
  return state.result;
}

std::string findXPathForProgressInternal(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                         const float intraSpineProgress) {
  const std::string tmpPath = decompressToTempFile(epub, spineIndex);
  if (tmpPath.empty()) {
    return "";
  }

  const size_t totalTextBytes = getTotalTextBytesCached(epub, spineIndex, tmpPath);
  if (totalTextBytes == 0) {
    Storage.remove(tmpPath.c_str());
    const std::string base = "/body/DocFragment[" + std::to_string(spineIndex + 1) + "]/body";
    LOG_DBG("KOX", "Forward: spine=%d no text, returning base xpath", spineIndex);
    return base;
  }

  const float clamped = std::max(0.0f, std::min(1.0f, intraSpineProgress));
  const size_t targetOffset = static_cast<size_t>(clamped * static_cast<float>(totalTextBytes));

  ForwardState state(spineIndex, targetOffset);
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    Storage.remove(tmpPath.c_str());
    return "";
  }

  state.parser = parser;
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, parserStartCb<ForwardState>, parserEndCb<ForwardState>);
  XML_SetCharacterDataHandler(parser, parserCharCb<ForwardState>);
  XML_SetDefaultHandlerExpand(parser, parserDefaultCb<ForwardState>);
  runParse(parser, tmpPath);
  XML_ParserFree(parser);
  Storage.remove(tmpPath.c_str());

  if (state.result.empty()) {
    state.result = "/body/DocFragment[" + std::to_string(spineIndex + 1) + "]/body";
  }

  LOG_DBG("KOX", "Forward: spine=%d progress=%.3f target=%zu/%zu -> %s", spineIndex, intraSpineProgress, targetOffset,
          totalTextBytes, state.result.c_str());
  return state.result;
}

// In-memory counterpart to countTotalTextBytes. Kept local to forward mapping since the full-file
// counter in ChapterXPathIndexerInternal.cpp is FsFile-based.
namespace {
struct BufferByteCounter {
  int skipDepth = -1;
  int bodyStartDepth = -1;
  int depth = 0;
  size_t totalTextBytes = 0;
};

void XMLCALL bbcStart(void* ud, const XML_Char* name, const XML_Char**) {
  auto* s = static_cast<BufferByteCounter*>(ud);
  const std::string tag = toLowerStr(name ? name : "");
  if (s->skipDepth < 0 && isSkippableTag(tag)) {
    s->skipDepth = s->depth;
  }
  if (tag == "body" && s->bodyStartDepth < 0) {
    s->bodyStartDepth = s->depth;
  }
  s->depth++;
}

void XMLCALL bbcEnd(void* ud, const XML_Char*) {
  auto* s = static_cast<BufferByteCounter*>(ud);
  s->depth--;
  if (s->skipDepth == s->depth) {
    s->skipDepth = -1;
  }
}

void XMLCALL bbcChar(void* ud, const XML_Char* text, const int len) {
  auto* s = static_cast<BufferByteCounter*>(ud);
  if (s->skipDepth >= 0 || s->bodyStartDepth < 0 || len <= 0 || isWhitespaceOnly(text, len)) {
    return;
  }
  s->totalTextBytes += countVisibleBytes(text, len);
}

size_t countTotalTextBytesInBuffer(const std::string& xhtml) {
  BufferByteCounter state;
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    return 0;
  }
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, bbcStart, bbcEnd);
  XML_SetCharacterDataHandler(parser, bbcChar);
  XML_Parse(parser, xhtml.data(), static_cast<int>(xhtml.size()), /*isFinal=*/1);
  XML_ParserFree(parser);
  return state.totalTextBytes;
}

bool runParseBuffer(XML_Parser parser, const std::string& xhtml) {
  const XML_Status status = XML_Parse(parser, xhtml.data(), static_cast<int>(xhtml.size()), /*isFinal=*/1);
  if (status == XML_STATUS_ERROR) {
    // XML_ERROR_ABORTED is the expected signal from XML_StopParser-based early exit.
    return XML_GetErrorCode(parser) == XML_ERROR_ABORTED;
  }
  return true;
}
}  // namespace

std::string findXPathForParagraphFromBuffer(const int spineIndex, const uint16_t paragraphIndex,
                                            const std::string& xhtml, const float intraSpineProgress) {
  if (paragraphIndex == 0 || xhtml.empty()) {
    return "";
  }

  const bool wantOffset = intraSpineProgress >= 0.0f;
  size_t targetByteOffset = 0;
  if (wantOffset) {
    const size_t totalTextBytes = countTotalTextBytesInBuffer(xhtml);
    const float clamped = std::max(0.0f, std::min(1.0f, intraSpineProgress));
    targetByteOffset = static_cast<size_t>(clamped * static_cast<float>(totalTextBytes));
  }

  ParagraphState state(spineIndex, paragraphIndex, /*startParagraphCount=*/0, /*partialParse=*/false, wantOffset,
                       targetByteOffset);
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    return "";
  }
  state.parser = parser;
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, paragraphStartCb, paragraphEndCb);
  XML_SetCharacterDataHandler(parser, paragraphCharCb);
  XML_SetDefaultHandlerExpand(parser, parserDefaultCb<ParagraphState>);
  runParseBuffer(parser, xhtml);
  XML_ParserFree(parser);
  return state.result;
}

std::string findXPathForProgressFromBuffer(const int spineIndex, const float intraSpineProgress,
                                           const std::string& xhtml) {
  if (xhtml.empty()) {
    return "";
  }
  const size_t totalTextBytes = countTotalTextBytesInBuffer(xhtml);
  if (totalTextBytes == 0) {
    return "/body/DocFragment[" + std::to_string(spineIndex + 1) + "]/body";
  }
  const float clamped = std::max(0.0f, std::min(1.0f, intraSpineProgress));
  const size_t targetOffset = static_cast<size_t>(clamped * static_cast<float>(totalTextBytes));

  ForwardState state(spineIndex, targetOffset);
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    return "";
  }
  state.parser = parser;
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, parserStartCb<ForwardState>, parserEndCb<ForwardState>);
  XML_SetCharacterDataHandler(parser, parserCharCb<ForwardState>);
  XML_SetDefaultHandlerExpand(parser, parserDefaultCb<ForwardState>);
  runParseBuffer(parser, xhtml);
  XML_ParserFree(parser);

  if (state.result.empty()) {
    state.result = "/body/DocFragment[" + std::to_string(spineIndex + 1) + "]/body";
  }
  return state.result;
}

}  // namespace ChapterXPathIndexerInternal
