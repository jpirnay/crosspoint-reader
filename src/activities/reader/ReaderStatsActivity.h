#pragma once

#include "ReadingStats.h"
#include "activities/Activity.h"

/**
 * Read-only statistics screen showing reading activity.
 * When showSession is true (launched from reader), a "This session" section is shown.
 * When showSession is false (launched from Settings), only book + global totals are shown.
 */
class ReaderStatsActivity final : public Activity {
 public:
  ReaderStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const BookStats& bookStats,
                      const GlobalStats& globalStats, uint32_t sessionSeconds, uint32_t sessionPages,
                      uint32_t sessionWords, bool showSession = true)
      : Activity("ReaderStats", renderer, mappedInput),
        bookStats(bookStats),
        globalStats(globalStats),
        sessionSeconds(sessionSeconds),
        sessionPages(sessionPages),
        sessionWords(sessionWords),
        showSession(showSession) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  const BookStats bookStats;
  const GlobalStats globalStats;
  const uint32_t sessionSeconds;
  const uint32_t sessionPages;
  const uint32_t sessionWords;
  const bool showSession;
};
