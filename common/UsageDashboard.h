// Shared LVGL dashboard for both firmware-direct and firmware-relay - the
// two variants differ only in how they fetch a Snapshot (TLS straight to
// claude.ai vs. a plain HTTP GET to a local relay), never in how it's
// drawn. Referenced by both PlatformIO projects via lib_extra_dirs, same
// pattern as TouchWifiProvisioner itself.
#pragma once

#include <Arduino.h>
#include <functional>
#include <lvgl.h>

namespace UsageDashboard {

struct Snapshot {
  bool valid = false;   // false until the first successful fetch
  String error;         // set (and valid left false) when a fetch fails

  int sessionPercent = 0;
  String sessionResetsIn;  // pre-formatted, e.g. "4h 37m" - this module never touches time

  int weeklyPercent = 0;
  String weeklyResetsIn;

  bool creditsEnabled = false;
  long creditsUsedMinor = 0;   // smallest currency unit (e.g. cents)
  long creditsLimitMinor = 0;
  String currency = "USD";
};

// Builds the static layout as a child of `parent`. Call once, after LVGL
// display/indev are registered.
void build(lv_obj_t *parent);

// Repaints bar values and labels from a fresh snapshot. Cheap - never
// rebuilds the layout, safe to call every poll.
void update(const Snapshot &snap);

// Small line under the meters - wifi/poll status, not part of Snapshot
// since it reflects transport state the fetch layer knows about that a
// Snapshot alone doesn't (e.g. "Connecting...", "Wi-Fi lost").
void setStatusLine(const String &text);

// Fires when the user taps the settings gear (top-right).
void setOnSettingsClicked(std::function<void()> cb);

} // namespace UsageDashboard
