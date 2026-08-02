// Shared LVGL dashboard for every board firmware-direct supports (CYD,
// CrowPanel7, D1 mini) - board bring-up differs, but a Snapshot is drawn
// the same way regardless. Referenced via lib_extra_dirs, same pattern
// as TouchWifiProvisioner itself.
#pragma once

#include <Arduino.h>
#include <functional>
#include <lvgl.h>

namespace UsageDashboard {

struct Snapshot {
  bool valid = false;   // false until the first successful fetch
  String error;         // set (and valid left false) when a fetch fails

  int sessionPercent = 0;
  // Full sub-label text, not just a bare value - e.g. "Resets in 4h 37m"
  // for a countdown or "Resets Thursday 4PM" for a clock time. This
  // module never touches time/timezones itself, so it can't supply the
  // right connector word for whichever style the fetch client picked;
  // each Snapshot producer builds the complete phrase.
  String sessionResetsIn;

  int weeklyPercent = 0;
  String weeklyResetsIn;  // same contract as sessionResetsIn above

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

// Fires when the user taps anywhere else on the dashboard (background,
// meters) - a manual "refresh now" gesture instead of waiting for the
// next scheduled poll. The gear button's own tap keeps opening settings
// only, not this - LVGL dispatches a tap to the deepest clickable object
// under it, and the gear is its own clickable object.
void setOnBackgroundClicked(std::function<void()> cb);

} // namespace UsageDashboard
