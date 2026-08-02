// Minimal RFC3339 parser for the fixed shape claude.ai emits:
// "2026-08-01T15:50:00[.ffffff]+HH:MM". No general-purpose
// timezone/DST handling - just enough to turn a resets_at string into a
// UTC epoch so it can be diffed against the device's NTP-synced clock.
#pragma once

#include <Arduino.h>

namespace Iso8601 {

// Returns 0 if `s` doesn't match the expected shape.
time_t parseUtc(const String &s);

} // namespace Iso8601
