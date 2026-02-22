#pragma once

#include <InflateReader.h>
#include <WiFi.h>

// Thin helpers that wrap WiFi mode changes with inflate ring-buffer lifecycle.
//
// The DEFLATE ring buffer (32 KB) and the WiFi/TLS stack compete for the same
// limited DRAM heap on the ESP32-C3.
//
// enableSTA/enableAP do NOT free the ring buffer before starting WiFi.
// Freeing it changes the heap layout, which can place WiFi driver allocations
// adjacent to other objects; a known minor WiFi driver overrun then corrupts
// their heap headers, crashing during cleanup (multi_heap_free assert).
// Most activities (WebServer, WifiSelection, OPDS, Calibre) use no TLS and
// run fine with the ring buffer in place.
//
// disable() reclaims the ring buffer after WiFi stops. When the buffer was
// already allocated (common case) claimSharedBuffer() is a no-op. When it
// was null (e.g. first EPUB open after a TLS session that had explicitly
// yielded it), the claim grabs the defragmented heap before it re-fragments.
//
// Activities that use TLS (KOReaderSync, OTA) can explicitly call
// InflateReader::yieldSharedBuffer() before enableSTA() to give TLS more
// contiguous heap, and rely on disable() to reclaim it afterward.
//
// Usage — replace raw WiFi.mode() calls with these helpers:
//   WiFiNetwork::enableSTA()  instead of  WiFi.mode(WIFI_STA)
//   WiFiNetwork::enableAP()   instead of  WiFi.mode(WIFI_AP)
//   WiFiNetwork::disable()    instead of  WiFi.mode(WIFI_OFF)
//
// All other WiFi calls (disconnect, begin, softAP, etc.) remain unchanged.
namespace WiFiNetwork {

inline void enableSTA() { WiFi.mode(WIFI_STA); }

inline void enableAP() { WiFi.mode(WIFI_AP); }

// Call instead of WiFi.mode(WIFI_OFF). Reclaims the ring buffer so it is
// available before the heap can re-fragment. The short delay lets the WiFi
// driver finish its FreeRTOS-task-based memory cleanup before malloc is called.
inline void disable() {
  WiFi.mode(WIFI_OFF);
  delay(100);
  InflateReader::claimSharedBuffer();
}

}  // namespace WiFiNetwork
