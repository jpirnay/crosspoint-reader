#include "FileBrowserActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <numeric>

#include "../util/ConfirmationActivity.h"
#include "BookInfoActivity.h"
#include "FileBrowserMenuActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;

// Natural case-insensitive comparison for two C strings.
bool naturalLess(const char* s1, const char* s2) {
  while (*s1 && *s2) {
    if (isdigit(*s1) && isdigit(*s2)) {
      while (*s1 == '0') s1++;
      while (*s2 == '0') s2++;
      int len1 = 0, len2 = 0;
      while (isdigit(s1[len1])) len1++;
      while (isdigit(s2[len2])) len2++;
      if (len1 != len2) return len1 < len2;
      for (int i = 0; i < len1; i++) {
        if (s1[i] != s2[i]) return s1[i] < s2[i];
      }
      s1 += len1;
      s2 += len2;
    } else {
      char c1 = tolower(*s1), c2 = tolower(*s2);
      if (c1 != c2) return c1 < c2;
      s1++;
      s2++;
    }
  }
  return *s1 == '\0' && *s2 != '\0';
}

// Return the display name for sorting (no extension, no trailing slash).
std::string sortName(const std::string& entry) {
  if (entry.back() == '/') return entry.substr(0, entry.length() - 1);
  const auto dot = entry.rfind('.');
  return (dot != std::string::npos) ? entry.substr(0, dot) : entry;
}

// Strip a leading article from a title string for locale-aware title sort.
// Handles English ("The", "A", "An") and German ("Der", "Die", "Das", "Ein", "Eine").
std::string_view stripArticle(std::string_view title) {
  static const char* articles[] = {
      "the ", "a ",   "an ",                   // English
      "der ", "die ", "das ", "ein ", "eine "  // German
  };
  // Build a lowercase prefix (max 6 chars needed) for matching
  char lower[7] = {};
  for (size_t i = 0; i < title.size() && i < 6; ++i) lower[i] = static_cast<char>(tolower(title[i]));

  for (const char* art : articles) {
    const size_t len = strlen(art);
    if (title.size() > len && strncmp(lower, art, len) == 0) {
      return title.substr(len);
    }
  }
  return title;
}

// Returns the sort key for title-based sorting.
std::string titleSortKey(const std::string& entry) {
  const std::string name = sortName(entry);
  const std::string_view stripped = stripArticle(name);
  std::string key(stripped);
  for (char& c : key) c = static_cast<char>(tolower(c));
  return key;
}

}  // namespace

void FileBrowserActivity::loadFiles() {
  files.clear();
  fileDates.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  root.rewindDirectory();

  char name[500];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    if (name[0] == '.' || strcmp(name, "System Volume Information") == 0) {
      file.close();
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(std::string(name) + "/");
      fileDates.push_back(0);
    } else {
      std::string_view filename{name};
      if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
          FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
          FsHelpers::hasBmpExtension(filename)) {
        files.emplace_back(filename);
        fileDates.push_back(file.getModifyDateTime());
      }
    }
    file.close();
  }
  root.close();

  // Sort using an index permutation so files and fileDates stay in sync.
  std::vector<size_t> idx(files.size());
  std::iota(idx.begin(), idx.end(), 0);

  std::sort(idx.begin(), idx.end(), [this](size_t a, size_t b) {
    const bool dirA = files[a].back() == '/';
    const bool dirB = files[b].back() == '/';
    // Directories always come first, sorted by name.
    if (dirA != dirB) return dirA;
    if (dirA && dirB) return naturalLess(files[a].c_str(), files[b].c_str());

    switch (sortOrder) {
      case FileBrowserSortOrder::NAME_DESC:
        return naturalLess(files[b].c_str(), files[a].c_str());
      case FileBrowserSortOrder::TITLE_ASC: {
        const auto ka = titleSortKey(files[a]);
        const auto kb = titleSortKey(files[b]);
        return naturalLess(ka.c_str(), kb.c_str());
      }
      case FileBrowserSortOrder::TITLE_DESC: {
        const auto ka = titleSortKey(files[a]);
        const auto kb = titleSortKey(files[b]);
        return naturalLess(kb.c_str(), ka.c_str());
      }
      case FileBrowserSortOrder::DATE_NEWEST:
        return fileDates[a] > fileDates[b];
      case FileBrowserSortOrder::DATE_OLDEST:
        return fileDates[a] < fileDates[b];
      case FileBrowserSortOrder::NAME_ASC:
      default:
        return naturalLess(files[a].c_str(), files[b].c_str());
    }
  });

  std::vector<std::string> sortedFiles;
  std::vector<uint32_t> sortedDates;
  sortedFiles.reserve(files.size());
  sortedDates.reserve(fileDates.size());
  for (const size_t i : idx) {
    sortedFiles.push_back(std::move(files[i]));
    sortedDates.push_back(fileDates[i]);
  }
  files = std::move(sortedFiles);
  fileDates = std::move(sortedDates);
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();

  loadFiles();
  selectorIndex = 0;

  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
}

void FileBrowserActivity::clearFileMetadata(const std::string& fullPath) {
  // Only clear cache for .epub files
  if (FsHelpers::hasEpubExtension(fullPath)) {
    Epub(fullPath, "/.crosspoint").clearCache();
    LOG_DBG("FileBrowser", "Cleared metadata cache for: %s", fullPath.c_str());
  }
}

void FileBrowserActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);

  // Long press BACK always navigates to home
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS) {
    onGoHome();
    return;
  }

  // Short press BACK goes up one directory (if not root) or home (at root)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < GO_HOME_MS) {
    if (basepath != "/") {
      const std::string oldPath = basepath;
      basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
      if (basepath.empty()) basepath = "/";
      loadFiles();
      const auto pos = oldPath.find_last_of('/');
      const std::string dirName = oldPath.substr(pos + 1) + "/";
      selectorIndex = findEntry(dirName);
      requestUpdate();
    } else {
      onGoHome();
    }
    return;
  }

  // Confirm short press opens selected entry; long press does nothing
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() < GO_HOME_MS) {
    if (files.empty()) return;

    const std::string& entry = files[selectorIndex];
    const bool isDirectory = (entry.back() == '/');

    if (isDirectory) {
      if (basepath.back() != '/') basepath += "/";
      basepath += entry.substr(0, entry.length() - 1);
      loadFiles();
      selectorIndex = 0;
      requestUpdate();
    } else {
      std::string fullPath = basepath;
      if (fullPath.back() != '/') fullPath += "/";
      onSelectBook(fullPath + entry);
    }
    return;
  }

  // Left short press opens the context menu
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) && mappedInput.getHeldTime() < GO_HOME_MS) {
    openMenu();
    return;
  }

  // Right opens the info page for epub files
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (files.empty()) return;
    const std::string& entry = files[selectorIndex];
    if (entry.back() != '/' && (FsHelpers::hasEpubExtension(entry) || FsHelpers::hasXtcExtension(entry))) {
      std::string cleanBase = basepath;
      if (cleanBase.back() != '/') cleanBase += "/";
      startActivityForResult(std::make_unique<BookInfoActivity>(renderer, mappedInput, cleanBase + entry),
                             [this](const ActivityResult&) { requestUpdate(); });
    }
    return;
  }

  // Up/Down side buttons navigate the list
  const int listSize = static_cast<int>(files.size());
  buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onContinuous({MappedInputManager::Button::Down}, [this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onContinuous({MappedInputManager::Button::Up}, [this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void FileBrowserActivity::deleteSelected() {
  const std::string& entry = files[selectorIndex];
  const bool isDirectory = (entry.back() == '/');

  std::string cleanBase = basepath;
  if (cleanBase.back() != '/') cleanBase += "/";
  const std::string fullPath = cleanBase + (isDirectory ? entry.substr(0, entry.length() - 1) : entry);

  auto handler = [this, fullPath, isDirectory](const ActivityResult& res) {
    if (!res.isCancelled) {
      LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
      clearFileMetadata(fullPath);
      const bool ok = isDirectory ? Storage.removeDir(fullPath.c_str()) : Storage.remove(fullPath.c_str());
      if (ok) {
        LOG_DBG("FileBrowser", "Deleted successfully");
        loadFiles();
        if (files.empty()) {
          selectorIndex = 0;
        } else if (selectorIndex >= files.size()) {
          selectorIndex = files.size() - 1;
        }
        requestUpdate(true);
      } else {
        LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
      }
    }
  };

  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE) + std::string("? "),
                                             isDirectory ? entry.substr(0, entry.length() - 1) : entry),
      handler);
}

void FileBrowserActivity::openMenu() {
  const std::string selectedName =
      files.empty()
          ? std::string(tr(STR_SD_CARD))
          : (files[selectorIndex].back() == '/' ? files[selectorIndex].substr(0, files[selectorIndex].length() - 1)
                                                : files[selectorIndex]);
  const bool isDirectory = !files.empty() && files[selectorIndex].back() == '/';

  startActivityForResult(
      std::make_unique<FileBrowserMenuActivity>(renderer, mappedInput, selectedName, isDirectory, sortOrder),
      [this](const ActivityResult& res) {
        if (res.isCancelled) return;
        const auto action = static_cast<FileBrowserMenuActivity::MenuAction>(std::get<MenuResult>(res.data).action);
        switch (action) {
          case FileBrowserMenuActivity::MenuAction::SORT_NAME_ASC:
            sortOrder = FileBrowserSortOrder::NAME_ASC;
            break;
          case FileBrowserMenuActivity::MenuAction::SORT_NAME_DESC:
            sortOrder = FileBrowserSortOrder::NAME_DESC;
            break;
          case FileBrowserMenuActivity::MenuAction::SORT_TITLE_ASC:
            sortOrder = FileBrowserSortOrder::TITLE_ASC;
            break;
          case FileBrowserMenuActivity::MenuAction::SORT_TITLE_DESC:
            sortOrder = FileBrowserSortOrder::TITLE_DESC;
            break;
          case FileBrowserMenuActivity::MenuAction::SORT_DATE_NEWEST:
            sortOrder = FileBrowserSortOrder::DATE_NEWEST;
            break;
          case FileBrowserMenuActivity::MenuAction::SORT_DATE_OLDEST:
            sortOrder = FileBrowserSortOrder::DATE_OLDEST;
            break;
          case FileBrowserMenuActivity::MenuAction::DELETE:
            if (!files.empty()) deleteSelected();
            return;
        }
        // Re-sort and keep the same entry selected if possible
        const std::string selectedEntry = files.empty() ? std::string{} : files[selectorIndex];
        loadFiles();
        selectorIndex = selectedEntry.empty() ? 0 : findEntry(selectedEntry);
        requestUpdate(true);
      });
}

std::string getFileName(std::string filename) {
  if (filename.back() == '/') {
    return filename.substr(0, filename.length() - 1);
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

void FileBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName = (basepath == "/") ? tr(STR_SD_CARD) : basepath.substr(basepath.rfind('/') + 1);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (files.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_FILES_FOUND));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, files.size(), selectorIndex,
        [this](int index) { return getFileName(files[index]); }, nullptr,
        [this](int index) { return UITheme::getFileIcon(files[index]); });
  }

  // Side buttons (Up/Down) navigate; show their hints on the side
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  // Front buttons: Back=Back(subdir)/Home(root), Confirm=Open, Left=Menu, Right=Info
  const bool hasInfo =
      !files.empty() && files[selectorIndex].back() != '/' &&
      (FsHelpers::hasEpubExtension(files[selectorIndex]) || FsHelpers::hasXtcExtension(files[selectorIndex]));
  const auto labels =
      mappedInput.mapLabels(basepath == "/" ? tr(STR_HOME) : tr(STR_BACK), files.empty() ? "" : tr(STR_OPEN),
                            tr(STR_MENU), hasInfo ? tr(STR_INFO) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}