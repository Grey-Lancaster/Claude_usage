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

// Small row under the meters - wifi/poll status, not part of Snapshot
// since it reflects transport state the fetch layer knows about that a
// Snapshot alone doesn't (e.g. "Connecting...", "Wi-Fi lost"). Occupies
// the full row via the left slot; clears the countdown/uptime slots
// setLiveStatus() below drives, since a single free-form message and a
// live countdown don't make sense on screen at the same time.
void setStatusLine(const String &text);

// Sets the row's left slot to "Updated" - call once per successful
// fetch. Leaves the countdown/uptime slots alone; setLiveStatus() below
// owns those independently since they tick every second, not once per
// poll.
void setUpdatedLabel();

// Ticks the row's other slot(s) - call every ~1s once the dashboard is
// live (first successful fetch has happened). `countdownText` (e.g.
// "Next update in 4:52") goes in the row's middle slot on wide displays
// (>=480px) or its right slot on narrow ones. `uptimeText` only appears
// on wide displays, which have room for a third slot - passed but
// ignored elsewhere, since narrow displays don't have anywhere to put
// it (see this file's largeDisplay checks).
void setLiveStatus(const String &countdownText, const String &uptimeText);

// Fires when the user taps the settings gear (top-right).
void setOnSettingsClicked(std::function<void()> cb);

// Fires when the user taps anywhere else on the dashboard (background,
// meters) - a manual "refresh now" gesture instead of waiting for the
// next scheduled poll. The gear button's own tap keeps opening settings
// only, not this - LVGL dispatches a tap to the deepest clickable object
// under it, and the gear is its own clickable object.
void setOnBackgroundClicked(std::function<void()> cb);

} // namespace UsageDashboard
