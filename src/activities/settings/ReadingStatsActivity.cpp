#include "ReadingStatsActivity.h"

#include <Arduino.h>  // millis()
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "MappedInputManager.h"
#include "ReadingSessionTracker.h"
#include "ReadingStats.h"
#include "ReadingStatsBookListActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// "1h 23m" / "23m 45s" / "12s" — Phase 1 keeps it compact so a long row fits
// the right column without truncation on the X3's narrow screen.
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

}  // namespace

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void ReadingStatsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  // Confirm opens the all-books list when there's anything to drill into.
  // Suppressed when the store is empty so the button hint never lies.
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && !READING_STATS.getBooks().empty()) {
    startActivityForResult(std::make_unique<ReadingStatsBookListActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { requestUpdate(); });
    return;
  }
  // If a reading session happens to be live (e.g. a future entry point lets
  // the user pop this screen mid-read), tick at most once per second so
  // "this session" moves visibly without hammering the e-ink panel.
  if (globalReadingSessionTracker().isActive()) {
    const uint32_t now = millis();
    if (now - lastLiveRefreshMs >= 1000) {
      lastLiveRefreshMs = now;
      requestUpdate();
    }
  }
}

void ReadingStatsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect contentRect = UITheme::getContentRect(renderer, /*hasBottomHints=*/true, /*hasSideHints=*/false);

  renderer.clearScreen();

  GUI.drawHeader(renderer,
                 Rect{contentRect.x, contentRect.y + metrics.topPadding, contentRect.width, metrics.headerHeight},
                 tr(STR_READING_STATS), nullptr);

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

  const auto& store = READING_STATS;
  auto& tracker = globalReadingSessionTracker();

  // ---- Live session (only if currently reading) ----
  if (tracker.isActive()) {
    drawSection(tr(STR_READING_STATS_CURRENT_SESSION));
    drawRow(tr(STR_READING_STATS_TOTAL_TIME), formatDuration(tracker.getLiveSeconds()));
    drawRow(tr(STR_READING_STATS_PAGES), std::to_string(tracker.getLivePages()));
  }

  // ---- All time ----
  drawSection(tr(STR_READING_STATS_TOTAL_TIME));
  if (store.getGlobalTotalSeconds() == 0) {
    drawRow("", tr(STR_READING_STATS_NO_DATA));
  } else {
    drawRow(tr(STR_READING_STATS_TOTAL_TIME), formatDuration(store.getGlobalTotalSeconds()));
    drawRow(tr(STR_READING_STATS_SESSIONS), std::to_string(store.getGlobalTotalSessions()));
    drawRow(tr(STR_READING_STATS_PAGES), std::to_string(store.getGlobalTotalPagesTurned()));
    drawRow(tr(STR_READING_STATS_BOOKS), std::to_string(store.getBookCount()));

    // Streaks: only meaningful when at least one wall-clocked session exists.
    if (!store.getGlobalDays().empty()) {
      const uint16_t today = currentLocalDayIndex();
      const uint16_t current = store.computeCurrentStreak(today);
      const uint16_t longest = store.computeLongestStreak();
      char buf[24];
      snprintf(buf, sizeof(buf), "%u%s / %u%s", current, tr(STR_READING_STATS_DAYS_UNIT), longest,
               tr(STR_READING_STATS_DAYS_UNIT));
      drawRow(tr(STR_READING_STATS_STREAK), buf);
    }
  }

  // ---- 30-day sparkline ----
  // Renders one bar per day for the last 30 local days ending at "today".
  // Height of each bar is proportional to that day's seconds vs. the maximum
  // seen in the window. Days with no reading get a flat 1px baseline so the
  // gap pattern stays visible. Drawn only when the clock is synced — without
  // it we have no "today" to anchor the window against.
  const uint16_t today = currentLocalDayIndex();
  if (today != 0 && !store.getGlobalDays().empty()) {
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
      const uint16_t d =
          (today > static_cast<uint16_t>(kSparkDays - 1 - i)) ? static_cast<uint16_t>(today - (kSparkDays - 1 - i)) : 0;
      const uint32_t s = store.getSecondsForDay(d);
      if (s > maxSeconds) maxSeconds = s;
    }

    // Baseline (axis) — 1px line under the bars so the visual grouping reads
    // as a chart even when most days are empty.
    renderer.drawLine(sparkOriginX, sparkOriginY + kSparkHeight, sparkOriginX + totalSpan, sparkOriginY + kSparkHeight,
                      true);
    for (int i = 0; i < kSparkDays; ++i) {
      const uint16_t d =
          (today > static_cast<uint16_t>(kSparkDays - 1 - i)) ? static_cast<uint16_t>(today - (kSparkDays - 1 - i)) : 0;
      const uint32_t s = store.getSecondsForDay(d);
      const int barX = sparkOriginX + i * (barWidth + kBarGap);
      // 1px minimum so empty days still tick on the axis.
      const int h = s == 0 ? 1 : std::max<int>(2, (s * kSparkHeight) / maxSeconds);
      renderer.fillRect(barX, sparkOriginY + kSparkHeight - h, barWidth, h, true);
    }
    y += kSparkHeight + 6;
  }

  // ---- Top books (up to 3 by total time) ----
  if (!store.getBooks().empty()) {
    std::vector<const BookReadingStats*> sorted;
    sorted.reserve(store.getBooks().size());
    for (const auto& b : store.getBooks()) sorted.push_back(&b);
    std::sort(sorted.begin(), sorted.end(),
              [](const BookReadingStats* a, const BookReadingStats* b) { return a->totalSeconds > b->totalSeconds; });

    drawSection(tr(STR_READING_STATS_TOP_BOOKS));
    const size_t shown = std::min<size_t>(sorted.size(), 3);
    for (size_t i = 0; i < shown; ++i) {
      const auto* b = sorted[i];
      // Use the title when known; fall back to docId so the row is never
      // empty even before metadata is recorded.
      std::string label = b->title.empty() ? b->docId : b->title;
      // Trim to fit; 22 chars keeps it in the left column at UI_10.
      if (label.size() > 22) {
        label.resize(22);
        label += "…";
      }
      drawRow(label.c_str(), formatDuration(b->totalSeconds));
    }
  }

  const char* btn2 = store.getBooks().empty() ? "" : tr(STR_READING_STATS_BOOK_LIST);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), btn2, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
