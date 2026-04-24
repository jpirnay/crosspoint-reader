#pragma once

#include <Epub.h>

#include <memory>
#include <string>

namespace ChapterXPathIndexerInternal {

bool findProgressForXPathInternal(const std::shared_ptr<Epub>& epub, int spineIndex, const std::string& xpath,
                                  float& outIntraSpineProgress, bool& outExactMatch);

// Host-test entry point: reverse-map an XPath against an in-memory XHTML buffer without going
// through the EPUB/SD stack. Matches the behavior of findProgressForXPathInternal.
bool findProgressForXPathFromBuffer(int spineIndex, const std::string& xpath, const std::string& xhtml,
                                    float& outIntraSpineProgress, bool& outExactMatch);

}  // namespace ChapterXPathIndexerInternal
