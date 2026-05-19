#pragma once

#include <string>

namespace StringUtils {

/**
 * Sanitize a string for use as a filename.
 * Replaces invalid characters with underscores, trims spaces/dots,
 * and limits length to maxBytes bytes.
 */
std::string sanitizeFilename(const std::string& name, size_t maxBytes = 100);

/**
 * Normalize a device name into a hostname/device-id form: lowercase, alphanumeric
 * kept, all other characters collapsed into a single '-' (no leading/trailing dashes).
 * Falls back to "crosspoint" if the result would otherwise be empty.
 * Used for mDNS hostname, KOReader device_id, and any other place that needs an
 * RFC-1123 compatible identifier derived from the user-configured device name.
 */
std::string makeHostname(const char* name);

}  // namespace StringUtils
