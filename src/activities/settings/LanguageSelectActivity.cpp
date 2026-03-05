#include "LanguageSelectActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>

#include "I18nKeys.h"
#include "LanguageDownloadActivity.h"
#include "MappedInputManager.h"
#include "fontIds.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns true if the language at the given ALL_LANGUAGES index is available
// for use — either it is a core (flash) language or its YAML is on the SD card.
bool LanguageSelectActivity::isInstalled(uint8_t metaIndex) const {
  if (ALL_LANGUAGES[metaIndex].core) return true;

  char path[48];
  snprintf(path, sizeof(path), "/.crosspoint/languages/%s.yaml", ALL_LANGUAGES[metaIndex].code);

  FsFile f;
  if (!Storage.openFileForRead("LANG", path, f)) return false;
  f.close();
  return true;
}

// ---------------------------------------------------------------------------
// Activity lifecycle
// ---------------------------------------------------------------------------

void LanguageSelectActivity::onEnter() {
  Activity::onEnter();

  // Find the display position of the active language in SORTED_ALL_LANGUAGE_INDICES.
  const char* activeCode = I18N.getActiveCode();
  selectedIndex = 0;
  for (uint8_t i = 0; i < totalItems; i++) {
    const uint8_t mi = SORTED_ALL_LANGUAGE_INDICES[i];
    if (strcmp(ALL_LANGUAGES[mi].code, activeCode) == 0) {
      selectedIndex = static_cast<int>(i);
      break;
    }
  }

  requestUpdate();
}

void LanguageSelectActivity::onExit() { Activity::onExit(); }

void LanguageSelectActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
    requestUpdate();
  });
}

void LanguageSelectActivity::handleSelection() {
  const uint8_t metaIndex = SORTED_ALL_LANGUAGE_INDICES[selectedIndex];
  const LanguageMeta& meta = ALL_LANGUAGES[metaIndex];

  if (meta.core) {
    // Core language: activate immediately via enum index (== metaIndex for core)
    RenderLock lock(*this);
    I18N.setLanguage(static_cast<Language>(metaIndex));
    onBack();
    return;
  }

  if (isInstalled(metaIndex)) {
    // Non-core language already on SD card: load it.
    RenderLock lock(*this);
    if (!I18N.setExternalLanguage(meta.code)) {
      // Failed to load — stay on this screen, user will see no change.
      return;
    }
    onBack();
    return;
  }

  // Non-core language not installed: launch the download activity.
  startActivityForResult(std::make_unique<LanguageDownloadActivity>(renderer, mappedInput, meta.code, meta.name),
                         [this](const ActivityResult& res) {
                           if (!res.isCancelled) onBack();  // language activated — leave the list
                         });
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void LanguageSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LANGUAGE));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const char* activeCode = I18N.getActiveCode();

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, totalItems, selectedIndex,
      // Row title: native language name
      [](int index) {
        const uint8_t mi = SORTED_ALL_LANGUAGE_INDICES[index];
        return std::string(ALL_LANGUAGES[mi].name);
      },
      nullptr, nullptr,
      // Row status: active marker, or "(not installed)" for non-core
      [this, activeCode](int index) -> std::string {
        const uint8_t mi = SORTED_ALL_LANGUAGE_INDICES[index];
        if (strcmp(ALL_LANGUAGES[mi].code, activeCode) == 0) return tr(STR_SELECTED);
        if (!ALL_LANGUAGES[mi].core && !isInstalled(mi)) return tr(STR_DOWNLOAD);
        return "";
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
