#pragma once

#include <string>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

namespace BookFinished {
std::string findNextBookInDirectory(const std::string& currentBookPath, const std::string& currentBookSeries,
                                    const std::string& currentBookSeriesIndex);

bool moveFinishedBookToCompleted(const std::string& currentBookPath, std::string& outMovedPath);

enum class FinishedBookAction {
  Stay = 0,
  GoHome = 1,
  OpenNextBook = 2,
};
}  // namespace BookFinished

class FinishedBookActivity : public Activity {
 public:
  FinishedBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string currentBookPath,
                       std::string nextBookPath);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string currentBookPath_;
  std::string nextBookPath_;
  std::string nextBookName_;
  std::string nextBookTitle_;
  std::string nextBookAuthor_;
  std::string nextBookSeries_;
  std::string nextBookCoverPath_;
  bool nextBookAvailable_ = false;
  bool nextBookMetadataLoaded_ = false;
  bool moveFinishedBooksToCompleted_ = false;
  bool removeFinishedBooksFromRecents_ = false;
  int selectedIndex_ = 0;
};
