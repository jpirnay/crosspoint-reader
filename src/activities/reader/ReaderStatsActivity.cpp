#include "ReaderStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ReaderStatsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ReaderStatsActivity::onExit() { Activity::onExit(); }

void ReaderStatsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void ReaderStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_STATISTICS));

  const int marginLeft = metrics.contentSidePadding;
  constexpr int rowHeight = 26;
  const int sectionGap = metrics.verticalSpacing;
  int y = metrics.topPadding + metrics.headerHeight + sectionGap;

  // Helper: format seconds as "Xh Ym" or "Ym" if less than an hour
  auto formatTime = [](uint32_t seconds) -> std::string {
    const uint32_t minutes = seconds / 60;
    const uint32_t hours = minutes / 60;
    const uint32_t mins = minutes % 60;
    if (hours > 0) {
      return std::to_string(hours) + "h " + std::to_string(mins) + "m";
    }
    return std::to_string(mins) + "m";
  };

  // Helper: render a label+value row
  auto drawRow = [&](const char* label, const std::string& value) {
    renderer.drawText(UI_10_FONT_ID, marginLeft, y, label);
    const auto valueWidth = renderer.getTextWidth(UI_10_FONT_ID, value.c_str());
    renderer.drawText(UI_10_FONT_ID, pageWidth - metrics.contentSidePadding - valueWidth, y, value.c_str());
    y += rowHeight;
  };

  // Helper: render a bold section header
  auto drawSection = [&](const char* label) {
    renderer.drawText(UI_10_FONT_ID, marginLeft, y, label, true, EpdFontFamily::BOLD);
    y += rowHeight;
  };

  // --- This session ---
  if (showSession) {
    drawSection(tr(STR_STATS_SESSION));
    drawRow(tr(STR_STATS_TIME), formatTime(sessionSeconds));
    drawRow(tr(STR_STATS_PAGES), std::to_string(sessionPages));
    drawRow(tr(STR_STATS_WORDS), std::to_string(sessionWords));
    if (sessionSeconds >= 30 && sessionWords > 0) {
      const uint32_t wpm = (sessionWords * 60) / sessionSeconds;
      drawRow(tr(STR_STATS_SPEED), std::to_string(wpm) + " wpm");
    }
    y += sectionGap;
  }

  // --- Current book (always shown) ---
  // Merge session data into display values without mutating persisted stats
  const uint32_t displayBookSeconds = bookStats.totalReadingSeconds + (showSession ? sessionSeconds : 0);
  const uint32_t displayBookPages = bookStats.totalPagesRead + (showSession ? sessionPages : 0);
  const uint32_t displayBookWords = bookStats.totalWordsRead + (showSession ? sessionWords : 0);
  const uint16_t displayBookSessions = bookStats.sessionsCount + (showSession ? 1 : 0);

  drawSection(tr(STR_STATS_CURRENT_BOOK));
  drawRow(tr(STR_STATS_TIME), formatTime(displayBookSeconds));
  drawRow(tr(STR_STATS_PAGES), std::to_string(displayBookPages));
  drawRow(tr(STR_STATS_WORDS), std::to_string(displayBookWords));
  drawRow(tr(STR_STATS_SESSIONS), std::to_string(displayBookSessions));
  y += sectionGap;

  // --- All books ---
  const uint32_t displayGlobalSeconds = globalStats.totalReadingSeconds + (showSession ? sessionSeconds : 0);
  const uint32_t displayGlobalPages = globalStats.totalPagesRead + (showSession ? sessionPages : 0);
  const uint32_t displayGlobalWords = globalStats.totalWordsRead + (showSession ? sessionWords : 0);

  drawSection(tr(STR_STATS_ALL_BOOKS));
  drawRow(tr(STR_STATS_TIME), formatTime(displayGlobalSeconds));
  drawRow(tr(STR_STATS_PAGES), std::to_string(displayGlobalPages));
  drawRow(tr(STR_STATS_WORDS), std::to_string(displayGlobalWords));
  drawRow(tr(STR_STATS_STARTED), std::to_string(globalStats.booksStarted));
  drawRow(tr(STR_STATS_FINISHED), std::to_string(globalStats.booksFinished));

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
