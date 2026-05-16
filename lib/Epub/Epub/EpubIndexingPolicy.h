#pragma once
#include <stdint.h>

namespace EpubIndexingPolicy {
// Initial burst when opening an unindexed chapter — fast enough to not feel like a block.
constexpr uint8_t INITIAL_PAGES = 3;
constexpr uint32_t INITIAL_MAX_MS = 150;

// Background pump on the current chapter while idle.
constexpr uint8_t CURRENT_BG_PAGES = 10;
constexpr uint32_t CURRENT_BG_MAX_MS = 500;
constexpr uint32_t CURRENT_BG_INTERVAL_MS = 1000;

// Outrun: forward navigation hit a page not yet indexed.
constexpr uint8_t OUTRUN_PAGES = 5;
constexpr uint32_t OUTRUN_MAX_MS = 250;

// Next-chapter prewarm — lower priority, runs only after current chapter is complete.
constexpr uint8_t PREWARM_PAGES = 3;
constexpr uint32_t PREWARM_MAX_MS = 150;
constexpr uint32_t PREWARM_INTERVAL_MS = 1500;

// Heap guard — skip pumping below this free-heap threshold.
constexpr uint32_t MIN_FREE_HEAP_BYTES = 16 * 1024;
}  // namespace EpubIndexingPolicy
