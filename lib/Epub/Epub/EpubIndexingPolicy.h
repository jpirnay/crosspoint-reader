#pragma once
#include <stddef.h>
#include <stdint.h>

namespace EpubIndexingPolicy {
// Initial burst when opening an unindexed chapter — fast enough to not feel like a block.
constexpr uint8_t INITIAL_PAGES = 3;
constexpr uint32_t INITIAL_MAX_MS = 100;

// Background pump on the current chapter while idle.
// chunkBytes=32 keeps each SD-read+parse quantum under ~100ms on XML-heavy chapters
// so loop() returns quickly and button presses are not dropped.
constexpr uint8_t CURRENT_BG_PAGES = 3;
constexpr uint32_t CURRENT_BG_MAX_MS = 100;
constexpr size_t CURRENT_BG_CHUNK_BYTES = 32;
constexpr uint32_t CURRENT_BG_INTERVAL_MS = 200;

// Outrun: forward navigation hit a page not yet indexed.
constexpr uint8_t OUTRUN_PAGES = 5;
constexpr uint32_t OUTRUN_MAX_MS = 250;

// Flush: caller must reach the last page now (e.g. backward chapter skip).
// Use a large time budget per pump() call so the parser runs flat-out without
// the 50× overhead of OUTRUN_PAGES-sized slices.
constexpr uint8_t FLUSH_PAGES = 255;
constexpr uint32_t FLUSH_MAX_MS = 5000;

// Next-chapter prewarm — lower priority, runs only after current chapter is complete.
// chunkBytes=32 matches CURRENT_BG to keep each quantum short.
constexpr uint8_t PREWARM_PAGES = 3;
constexpr uint32_t PREWARM_MAX_MS = 100;
constexpr size_t PREWARM_CHUNK_BYTES = 32;
constexpr uint32_t PREWARM_INTERVAL_MS = 2000;

// Heap guard — skip pumping below this free-heap threshold.
constexpr uint32_t MIN_FREE_HEAP_BYTES = 16 * 1024;
}  // namespace EpubIndexingPolicy
