#include "InflateReader.h"

#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <type_traits>

static const char* TAG = "ZIP";

namespace {
constexpr size_t INFLATE_DICT_SIZE = 32768;

// A single ring buffer shared across all InflateReader instances.
// Lazily allocated from the heap and never freed by normal inflate operations.
// Use yieldSharedBuffer() / claimSharedBuffer() around memory-intensive network
// operations (e.g. WiFi + TLS) so those operations see a clean heap, and the
// buffer is reclaimed immediately after the network stack releases its memory.
// Safe to share because only one streaming inflate runs at a time.
uint8_t* s_sharedRingBuffer = nullptr;
}  // namespace

// Guarantee the cast pattern in the header comment is valid.
static_assert(std::is_standard_layout<InflateReader>::value,
              "InflateReader must be standard-layout for the uzlib callback cast to work");

InflateReader::~InflateReader() { deinit(); }

bool InflateReader::init(const bool streaming) {
  deinit();  // reset state

  if (streaming) {
    if (!s_sharedRingBuffer) {
      s_sharedRingBuffer = static_cast<uint8_t*>(malloc(INFLATE_DICT_SIZE));
      if (!s_sharedRingBuffer) return false;
    }
    ringBuffer = s_sharedRingBuffer;
    memset(ringBuffer, 0, INFLATE_DICT_SIZE);
  }

  uzlib_uncompress_init(&decomp, ringBuffer, ringBuffer ? INFLATE_DICT_SIZE : 0);
  return true;
}

void InflateReader::deinit() {
  // The shared ring buffer is managed via yieldSharedBuffer/claimSharedBuffer.
  ringBuffer = nullptr;
  memset(&decomp, 0, sizeof(decomp));
}

void InflateReader::yieldSharedBuffer() {
  free(s_sharedRingBuffer);
  s_sharedRingBuffer = nullptr;
}

bool InflateReader::claimSharedBuffer() {
  if (s_sharedRingBuffer) return true;
  s_sharedRingBuffer = static_cast<uint8_t*>(malloc(INFLATE_DICT_SIZE));
  if (!s_sharedRingBuffer) {
    ESP_LOGE(TAG, "claimSharedBuffer failed: free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  }
  return s_sharedRingBuffer != nullptr;
}

void InflateReader::setSource(const uint8_t* src, size_t len) {
  decomp.source = src;
  decomp.source_limit = src + len;
}

void InflateReader::setReadCallback(int (*cb)(struct uzlib_uncomp*)) { decomp.source_read_cb = cb; }

void InflateReader::skipZlibHeader() {
  uzlib_get_byte(&decomp);
  uzlib_get_byte(&decomp);
}

bool InflateReader::read(uint8_t* dest, size_t len) {
  if (!ringBuffer) {
    // One-shot mode: back-references use absolute offset from dest_start.
    // Valid only when read() is called once with the full output buffer.
    decomp.dest_start = dest;
  }
  decomp.dest = dest;
  decomp.dest_limit = dest + len;

  const int res = uzlib_uncompress(&decomp);
  if (res < 0) return false;
  return decomp.dest == decomp.dest_limit;
}

InflateStatus InflateReader::readAtMost(uint8_t* dest, size_t maxLen, size_t* produced) {
  if (!ringBuffer) {
    // One-shot mode: back-references use absolute offset from dest_start.
    // Valid only when readAtMost() is called once with the full output buffer.
    decomp.dest_start = dest;
  }
  decomp.dest = dest;
  decomp.dest_limit = dest + maxLen;

  const int res = uzlib_uncompress(&decomp);
  *produced = static_cast<size_t>(decomp.dest - dest);

  if (res == TINF_DONE) return InflateStatus::Done;
  if (res < 0) return InflateStatus::Error;
  return InflateStatus::Ok;
}
