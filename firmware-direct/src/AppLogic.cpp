#include "AppLogic.h"

#include <ArduinoOTA.h>
#include <ezTime.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <TouchWifiProvisioner.h>

#include "ClaudeConfig.h"
#include "ClaudeSetupServer.h"
#include "ClaudeUsageClient.h"
#include "OtaPassword.h"
#include "UsageDashboard.h"

using namespace ezt;

namespace AppLogic {
namespace {

// Drives both the display's "resets in"-as-absolute-time formatting
// (ClaudeUsageClient reads this via ezTime's default-timezone bounce-
// through functions, set via setDefault() below) and is re-pointed live
// when the timezone is changed from the setup page.
Timezone myTZ;

static const unsigned long POLL_INTERVAL_MS = 5UL * 60UL * 1000UL;  // claude.ai is polled sparingly - this is an undocumented endpoint, not a rate-limit-friendly public API

lv_obj_t *settingsOverlay = nullptr;
unsigned long lastPollMs = 0;
bool dashboardReady = false;
std::function<void(WebServer &)> pendingScreenshotHandler;
String deviceIp;  // set once in onWifiConnected() - see setupUrls() below

// True once the first successful fetch has happened - gates
// tickLiveStatus()'s countdown/uptime display so it doesn't tick toward
// a poll that can't succeed while unconfigured/locked, and gets reset
// on account reset so a stale countdown doesn't immediately overwrite
// the "Not configured" message tickLiveStatus() would otherwise clobber
// a second later.
bool everUpdatedOnce = false;

// mDNS ("claudeusage.local") doesn't resolve on every network/device
// (some phones, some routers with mDNS reflection disabled, etc.) -
// showing the raw IP alongside it gives a fallback that always works.
String setupUrls() {
  String s = "http://claudeusage.local/";
  if (deviceIp.length() > 0) s += " or http://" + deviceIp + "/";
  return s;
}

void closeSettingsOverlay() {
  if (settingsOverlay) {
    lv_obj_del(settingsOverlay);
    settingsOverlay = nullptr;
  }
}

void onForgetWifiClicked(lv_event_t *e) {
  closeSettingsOverlay();
  TouchWifiProvisioner::reset();
}

void onResetAccountClicked(lv_event_t *e) {
  closeSettingsOverlay();
  ClaudeConfig::forget();
  everUpdatedOnce = false;
  UsageDashboard::setStatusLine("Not configured - visit " + setupUrls());
}

void onCloseSettingsClicked(lv_event_t *e) { closeSettingsOverlay(); }

void openSettingsOverlay() {
  if (settingsOverlay) return;

  settingsOverlay = lv_obj_create(lv_scr_act());
  lv_obj_add_flag(settingsOverlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(settingsOverlay, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(settingsOverlay, 0, 0);
  lv_obj_set_style_bg_color(settingsOverlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(settingsOverlay, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(settingsOverlay, 10, 0);
  lv_obj_set_flex_flow(settingsOverlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(settingsOverlay, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(settingsOverlay, 14, 0);

  lv_obj_t *title = lv_label_create(settingsOverlay);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_text(title, "Settings");
  lv_obj_set_style_pad_top(title, 10, 0);

  lv_obj_t *urlLabel = lv_label_create(settingsOverlay);
  lv_obj_set_style_text_color(urlLabel, lv_color_hex(0x999999), 0);
  lv_label_set_long_mode(urlLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(urlLabel, lv_pct(85));
  lv_obj_set_style_text_align(urlLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(urlLabel, ("Setup: " + setupUrls()).c_str());

  lv_obj_t *forgetBtn = lv_btn_create(settingsOverlay);
  lv_obj_set_size(forgetBtn, lv_pct(80), LV_SIZE_CONTENT);
  lv_obj_add_event_cb(forgetBtn, onForgetWifiClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *forgetLabel = lv_label_create(forgetBtn);
  lv_label_set_text(forgetLabel, "Forget Wi-Fi");
  lv_obj_center(forgetLabel);

  lv_obj_t *resetBtn = lv_btn_create(settingsOverlay);
  lv_obj_set_size(resetBtn, lv_pct(80), LV_SIZE_CONTENT);
  lv_obj_add_event_cb(resetBtn, onResetAccountClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *resetLabel = lv_label_create(resetBtn);
  lv_label_set_text(resetLabel, "Reset Claude Account");
  lv_obj_center(resetLabel);

  lv_obj_t *closeBtn = lv_btn_create(settingsOverlay);
  lv_obj_set_size(closeBtn, lv_pct(80), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(closeBtn, LV_OPA_20, 0);
  lv_obj_set_style_bg_color(closeBtn, lv_color_white(), 0);
  lv_obj_add_event_cb(closeBtn, onCloseSettingsClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *closeLabel = lv_label_create(closeBtn);
  lv_label_set_text(closeLabel, "Close");
  lv_obj_set_style_text_color(closeLabel, lv_color_white(), 0);
  lv_obj_center(closeLabel);
}

// --- Background usage fetch ---------------------------------------------
// ClaudeUsageClient::fetch() is a blocking HTTPS round trip (TLS handshake
// + response can run into multiple seconds). Calling it straight from
// pollUsage() used to block loop() - and with it lv_timer_handler() -  for
// the whole fetch, freezing all rendering. Most visibly this ate the tap
// ripple animation: manualRefresh() runs *inside* the same click-event
// dispatch that spawned the ripple, so LVGL never got a chance to paint an
// intermediate frame until the fetch finished, at which point real elapsed
// time had already blown past the animation's 400ms and it jumped straight
// to (or past) its end state. Less visibly, it also meant the whole
// touchscreen stopped responding to anything for the same duration on
// every periodic poll, not just manual refreshes.
//
// The network call now runs on its own FreeRTOS task so loop() keeps
// calling lv_timer_handler() at its normal ~5ms cadence throughout. Only
// plain data crosses task boundaries (org id/cookie one way, a Snapshot the
// other), each copied inside a short critical section; LVGL itself is only
// ever touched from the main task (the one running lv_timer_handler()).
portMUX_TYPE fetchMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool fetchRequested = false;
volatile bool fetchBusy = false;
String pendingOrgId;
String pendingCookie;
volatile bool fetchResultReady = false;
UsageDashboard::Snapshot fetchResultSnap;
bool fetchResultOk = false;

void fetchTaskFn(void *) {
  for (;;) {
    bool go = false;
    String orgId, cookie;

    portENTER_CRITICAL(&fetchMux);
    if (fetchRequested) {
      go = true;
      fetchRequested = false;
      fetchBusy = true;
      orgId = pendingOrgId;
      cookie = pendingCookie;
    }
    portEXIT_CRITICAL(&fetchMux);

    if (go) {
      UsageDashboard::Snapshot snap;
      bool ok = ClaudeUsageClient::fetch(orgId, cookie, snap);

      portENTER_CRITICAL(&fetchMux);
      fetchResultSnap = snap;
      fetchResultOk = ok;
      fetchResultReady = true;
      fetchBusy = false;
      portEXIT_CRITICAL(&fetchMux);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// millis()/1000 as a raw seconds count (e.g. "22200s") is unreadable past
// a few minutes - this breaks it into days/hours/minutes/seconds, omitting
// leading all-zero units (a fresh boot reads "12s", not "0d 0h 0m 12s").
String formatUptime(unsigned long totalSeconds) {
  unsigned long days = totalSeconds / 86400;
  unsigned long hours = (totalSeconds % 86400) / 3600;
  unsigned long minutes = (totalSeconds % 3600) / 60;
  unsigned long seconds = totalSeconds % 60;

  char buf[24];
  if (days > 0) {
    snprintf(buf, sizeof(buf), "%lud %luh %lum", days, hours, minutes);
  } else if (hours > 0) {
    snprintf(buf, sizeof(buf), "%luh %lum %lus", hours, minutes, seconds);
  } else if (minutes > 0) {
    snprintf(buf, sizeof(buf), "%lum %lus", minutes, seconds);
  } else {
    snprintf(buf, sizeof(buf), "%lus", seconds);
  }
  return String(buf) + " uptime";
}

// "M:SS" countdown to the next scheduled poll (lastPollMs is bumped to
// millis() every time pollUsage() actually proceeds past its throttle
// check, whether that's a real scheduled poll or a forced one - see
// pollUsage() below - so this is always ticking down to whichever comes
// first).
String formatCountdown() {
  long remainingMs = (long)(lastPollMs + POLL_INTERVAL_MS) - (long)millis();
  unsigned long remainingSec = remainingMs > 0 ? (unsigned long)(remainingMs / 1000) : 0;
  char buf[24];
  snprintf(buf, sizeof(buf), "Next update in %lu:%02lu", remainingSec / 60, remainingSec % 60);
  return String(buf);
}

// Called every loop() iteration, throttled to ~1s - keeps the
// countdown-to-next-poll and (on wide displays) uptime current between
// the much-less-frequent setUpdatedLabel() calls (once per successful
// fetch, ~5 minutes apart).
void tickLiveStatus() {
  static unsigned long lastTickMs = 0;
  if (!everUpdatedOnce) return;
  unsigned long now = millis();
  if (now - lastTickMs < 1000) return;
  lastTickMs = now;
  UsageDashboard::setLiveStatus(formatCountdown(), formatUptime(now / 1000));
}

// Drains a completed background fetch, if any, onto the dashboard. Called
// every loop() iteration - cheap (a flag check) when nothing's ready.
void checkFetchResult() {
  bool ready = false;
  UsageDashboard::Snapshot snap;
  bool ok = false;

  portENTER_CRITICAL(&fetchMux);
  if (fetchResultReady) {
    ready = true;
    snap = fetchResultSnap;
    ok = fetchResultOk;
    fetchResultReady = false;
  }
  portEXIT_CRITICAL(&fetchMux);

  if (!ready) return;
  Serial.printf("[app] pollUsage: fetch ok=%d snap.valid=%d snap.error='%s'\n", ok, snap.valid, snap.error.c_str());
  UsageDashboard::update(snap);
  ClaudeSetupServer::setLastFetchStatus(ok, snap.error);
  if (ok) {
    UsageDashboard::setUpdatedLabel();
    everUpdatedOnce = true;
  }
}

void pollUsage(bool force) {
  unsigned long now = millis();
  if (!force && now - lastPollMs < POLL_INTERVAL_MS) return;
  lastPollMs = now;

  if (!ClaudeConfig::isProvisioned()) {
    UsageDashboard::setStatusLine("Not configured - visit " + setupUrls());
    return;
  }
  if (!ClaudeConfig::isUnlocked()) {
    UsageDashboard::setStatusLine("Locked - visit " + setupUrls() + " to unlock");
    return;
  }
  if (fetchBusy || fetchRequested) return;  // one in flight/queued is enough

  String orgId = ClaudeConfig::orgId();
  String cookie = ClaudeConfig::cookie();
  portENTER_CRITICAL(&fetchMux);
  pendingOrgId = orgId;
  pendingCookie = cookie;
  fetchRequested = true;
  portEXIT_CRITICAL(&fetchMux);
}

// pollUsage(true) skips the normal 5-minute throttle entirely - fine for
// a one-off action like a successful unlock, but the tap-to-refresh
// gesture needs its own separate, much shorter cooldown so mashing the
// screen can't hammer an undocumented endpoint that isn't meant for
// frequent polling.
void manualRefresh() {
  static unsigned long lastManualMs = 0;
  unsigned long now = millis();
  if (now - lastManualMs < 3000) return;
  lastManualMs = now;
  pollUsage(true);
}

} // namespace

void begin() {
  // Pinned to core 0 - Arduino's setup()/loop() (and therefore
  // lv_timer_handler()) run as "loopTask" on core 1 by default, so this
  // keeps the network fetch fully off the UI's core.
  xTaskCreatePinnedToCore(fetchTaskFn, "usageFetch", 16384, nullptr, 1, nullptr, 0);
}

void setScreenshotHandler(std::function<void(WebServer &)> handler) {
  pendingScreenshotHandler = handler;
}

void onWifiConnected(const String &ip) {
  Serial.printf("Wi-Fi connected, IP: %s\n", ip.c_str());
  deviceIp = ip;  // reconnects can rotate the DHCP lease - keep this current
  // Blocks briefly (first connect only - already-synced time returns
  // immediately on reconnects) so the very first pollUsage() below
  // doesn't race ahead of NTP finishing. Without this, "resets in" shows
  // "?" until the next scheduled poll - up to 5 minutes later - recomputes
  // it with a now-valid clock. Also sets the system clock ezTime itself
  // reads from, replacing a plain configTime() call - Iso8601::parseUtc()
  // (mktime-based) and this all agree because nothing here ever moves the
  // system TZ away from UTC0; only myTZ's own offset/DST rules, applied
  // separately by ezTime when formatting, differ per zone.
  waitForSync(5);

  // TouchWifiProvisioner fires this callback again after every reconnect,
  // not just the first connection - the dashboard and web server only
  // need starting once; a reconnect should just let polling resume.
  if (!dashboardReady) {
    dashboardReady = true;
    UsageDashboard::build(lv_scr_act());
    UsageDashboard::setOnSettingsClicked(openSettingsOverlay);
    UsageDashboard::setOnBackgroundClicked(manualRefresh);

    // POSIX rule string (e.g. "EST5EDT,M3.2.0,M11.1.0"), not an IANA
    // name - setPosix() applies it fully offline. ezTime's own
    // setLocation() looks simpler but resolves an IANA name via a UDP
    // round-trip to ezTime's remote server, which measured unreliable on
    // this network - silently left the clock stuck on UTC with no error.
    myTZ.setPosix(ClaudeConfig::timezone());
    myTZ.setDefault();

    ClaudeSetupServer::setOnUnlocked([]() { pollUsage(true); });
    ClaudeSetupServer::setOnTimezoneChanged([](const String &tz) { myTZ.setPosix(tz); });
    if (pendingScreenshotHandler) ClaudeSetupServer::setScreenshotHandler(pendingScreenshotHandler);
    ClaudeSetupServer::begin();

    // Network OTA - "pio run -e cyd_ota -t upload" (or crowpanel7_ota)
    // instead of USB once the device is on Wi-Fi. Both boards currently
    // share the "claudeusage" hostname/mDNS name - fine with only one on
    // the network at a time, but running a CYD and a CrowPanel7
    // simultaneously would collide (whichever registered second loses).
    // Not fixed here since nobody's run two at once yet; worth revisiting
    // if that changes.
    ArduinoOTA.setHostname("claudeusage");
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
  }

  pollUsage(true);
}

void loop() {
  checkFetchResult();
  tickLiveStatus();
  TouchWifiProvisioner::loop();
  if (TouchWifiProvisioner::isConnected()) {
    events();  // services ezTime's background NTP re-sync scheduling
    ClaudeSetupServer::handleClient();
    ArduinoOTA.handle();
    pollUsage(false);
  }
  delay(5);
}

} // namespace AppLogic
