#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "FileBrowserMenuActivity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 private:
  void clearFileMetadata(const std::string& fullPath);
  void deleteSelected();
  void openMenu();

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::vector<uint32_t> fileDates;

  FileBrowserSortOrder sortOrder = FileBrowserSortOrder::NAME_ASC;

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/")
      : Activity("FileBrowser", renderer, mappedInput), basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
