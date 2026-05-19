#include "ReadingStatsBookDetailActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "MappedInputManager.h"
#include "ReadingStats.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

std::string formatDuration(uint32_t totalSeconds) {
  const uint32_t h = totalSeconds / 3600;
  const uint32_t m = (totalSeconds % 3600) / 60;
  const uint32_t s = totalSeconds % 60;
  char buf[24];
  if (h > 0) {
    snprintf(buf, sizeof(buf), "%uh %02um", h, m);
  } else if (m > 0) {
    snprintf(buf, sizeof(buf), "%um %02us", m, s);
  } else {
    snprintf(buf, sizeof(buf), "%us", s);
  }
  return buf;
}

// "today", "yesterday", "N days ago", or "YYYY-MM-DD" beyond a month.
// Returns "—" when epoch is 0 or the clock isn't synced.
std::string formatDateOrRelative(time_t epoch) {
  if (epoch == 0 || !HalClock::isSynced()) {
    return tr(STR_READING_STATS_UNKNOWN);
  }
  const time_t now = HalClock::now();
  if (now <= epoch) return "just now";
  const uint32_t delta = static_cast<uint32_t>(now - epoch);
  char buf[24];
  if (delta < 60) return "just now";
  if (delta < 3600) {
    snprintf(buf, sizeof(buf), "%um ago", delta / 60);
    return buf;
  }
  if (delta < 86400) {
    snprintf(buf, sizeof(buf), "%uh ago", delta / 3600);
    return buf;
  }
  const uint32_t days = delta / 86400;
  if (days < 30) {
    snprintf(buf, sizeof(buf), "%ud ago", days);
    return buf;
  }
  // Past a month, the relative form ("60d ago") is noisier than a date.
  struct tm t {};
  localtime_r(&epoch, &t);
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return buf;
}

uint32_t secondsForDayIn(const std::vector<DayBucket>& days, uint16_t dayIndex) {
  if (dayIndex == 0) return 0;
  auto it = std::lower_bound(days.begin(), days.end(), dayIndex,
                             [](const DayBucket& b, uint16_t v) { return b.dayIndex < v; });
  if (it != days.end() && it->dayIndex == dayIndex) return it->seconds;
  return 0;
}

}  // namespace

void ReadingStatsBookDetailActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ReadingStatsBookDetailActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
}

void ReadingStatsBookDetailActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, /*hasBottomHints=*/true, /*hasSideHints=*/false);
  const auto& store = READING_STATS;
  const BookReadingStats* book = store.findBook(docId);

  renderer.clearScreen();

  // Header: title up to ~28 chars (theme will further clip if the screen is
  // narrow). Fallback to docId so we still produce a usable screen if the
  // book's metadata was never recorded.
  std::string headerTitle = (book && !book->title.empty()) ? book->title : docId;
  if (headerTitle.size() > 28) {
    headerTitle.resize(28);
    headerTitle += "…";
  }
  GUI.drawHeader(renderer,
                 Rect{contentRect.x, contentRect.y + metrics.topPadding, contentRect.width, metrics.headerHeight},
                 headerTitle.c_str(), book && !book->author.empty() ? book->author.c_str() : nullptr);

  const int leftX = contentRect.x + metrics.verticalSpacing * 3;
  const int valueX = contentRect.x + contentRect.width / 2;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int rowStep = lineH + 2;
  const int subHeaderHeight = lineH + 6;
  int y = contentRect.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  auto drawSection = [&](const char* title) {
    GUI.drawSubHeader(renderer, Rect{contentRect.x, y, contentRect.width, subHeaderHeight}, title);
    y += subHeaderHeight + 2;
  };
  auto drawRow = [&](const char* label, const std::string& value) {
    renderer.drawText(UI_10_FONT_ID, leftX, y, label, true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, valueX, y, value.c_str());
    y += rowStep;
  };

  if (!book) {
    // The book may have been removed from the store between the list and
    // the detail screen (e.g. a future "clear stats for this book" action).
    // Show a placeholder rather than crash on a null deref.
    drawRow("", tr(STR_READING_STATS_NO_DATA));
  } else {
    drawSection(tr(STR_READING_STATS_TOTAL_TIME));
    drawRow(tr(STR_READING_STATS_TOTAL_TIME), formatDuration(book->totalSeconds));
    drawRow(tr(STR_READING_STATS_SESSIONS), std::to_string(book->sessions));
    drawRow(tr(STR_READING_STATS_PAGES), std::to_string(book->pagesTurned));
    if (book->sessions > 0) {
      drawRow(tr(STR_READING_STATS_AVG_SESSION), formatDuration(book->totalSeconds / book->sessions));
    }
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%u%%", book->progress);
    drawRow(tr(STR_READING_STATS_PROGRESS), pctBuf);

    drawSection(tr(STR_READING_STATS_FIRST_READ));
    drawRow(tr(STR_READING_STATS_FIRST_READ), formatDateOrRelative(book->firstReadEpoch));
    drawRow(tr(STR_READING_STATS_LAST_READ), formatDateOrRelative(book->lastReadEpoch));

    // Per-book 30-day sparkline. Identical algorithm to the main screen but
    // reads from this book's own day vector. Hidden when no wall-clocked day
    // exists or the clock isn't synced — otherwise the bars would be
    // meaningless ("we don't know what day this was").
    const uint16_t today = currentLocalDayIndex();
    if (today != 0 && !book->days.empty()) {
      drawSection(tr(STR_READING_STATS_LAST_30D));
      constexpr int kSparkDays = 30;
      constexpr int kSparkHeight = 38;
      constexpr int kBarGap = 1;
      const int sparkLeft = leftX;
      const int sparkRight = contentRect.x + contentRect.width - metrics.verticalSpacing * 3;
      const int sparkWidth = std::max(0, sparkRight - sparkLeft);
      const int barWidth = std::max(2, (sparkWidth - (kSparkDays - 1) * kBarGap) / kSparkDays);
      const int totalSpan = barWidth * kSparkDays + kBarGap * (kSparkDays - 1);
      const int sparkOriginX = sparkLeft + (sparkWidth - totalSpan) / 2;
      const int sparkOriginY = y;

      uint32_t maxSeconds = 1;
      for (int i = 0; i < kSparkDays; ++i) {
        const uint16_t d = (today > static_cast<uint16_t>(kSparkDays - 1 - i))
                               ? static_cast<uint16_t>(today - (kSparkDays - 1 - i))
                               : 0;
        const uint32_t s = secondsForDayIn(book->days, d);
        if (s > maxSeconds) maxSeconds = s;
      }

      renderer.drawLine(sparkOriginX, sparkOriginY + kSparkHeight, sparkOriginX + totalSpan,
                        sparkOriginY + kSparkHeight, true);
      for (int i = 0; i < kSparkDays; ++i) {
        const uint16_t d = (today > static_cast<uint16_t>(kSparkDays - 1 - i))
                               ? static_cast<uint16_t>(today - (kSparkDays - 1 - i))
                               : 0;
        const uint32_t s = secondsForDayIn(book->days, d);
        const int barX = sparkOriginX + i * (barWidth + kBarGap);
        const int h = s == 0 ? 1 : std::max<int>(2, (s * kSparkHeight) / maxSeconds);
        renderer.fillRect(barX, sparkOriginY + kSparkHeight - h, barWidth, h, true);
      }
      y += kSparkHeight + 6;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
