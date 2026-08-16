#include "AppLogic.h"

#include <ArduinoOTA.h>
#include <esp_log.h>
#include <esp_system.h>
#include <ezTime.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <TouchWifiProvisioner.h>
#include <WiFi.h>
#include "lwip/tcpip.h"

#include "ClaudeConfig.h"
#include "ClaudeSetupServer.h"
#include "ClaudeUsageClient.h"
#include "OtaPassword.h"
#include "UsageDashboard.h"
#include "Version.h"

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

// Physical BOOT button, GPIO0 on every board this project targets (CYD,
// D1 mini, CrowPanel7 all break it out the same way - it's the same pin
// used to enter the ROM bootloader for flashing, active-low with its own
// board-level pull-up). Same short-press/hold-to-reset pattern as the
// Weather project's boot.button handling: a short press opens System
// Info (see openSysInfoOverlay() below), holding it FACTORY_RESET_HOLD_MS
// wipes both the WiFi credentials and the Claude account and reboots.
constexpr int BOOT_BUTTON_PIN = 0;
constexpr unsigned long FACTORY_RESET_HOLD_MS = 10000;

lv_obj_t *sysInfoOverlay = nullptr;
lv_obj_t *sysInfoIpVal = nullptr;
lv_obj_t *sysInfoRssiVal = nullptr;
lv_obj_t *sysInfoHeapVal = nullptr;
lv_obj_t *sysInfoUptimeVal = nullptr;
lv_obj_t *sysInfoAccountVal = nullptr;

// True once the first successful fetch has happened - gates
// tickLiveStatus()'s countdown/uptime display so it doesn't tick toward
// a poll that can't succeed while unconfigured/locked, and gets reset
// on account reset so a stale countdown doesn't immediately overwrite
// the "Not configured" message tickLiveStatus() would otherwise clobber
// a second later.
bool everUpdatedOnce = false;

// The device relocks (in-RAM plaintext cookie wiped) on every boot by
// design - see ClaudeConfig.h - so a "Locked" screen the user didn't
// trigger themselves (forget/reset) means it rebooted, not that some
// unlock session timed out (there is no such timeout - unlocked only
// ever flips false in ClaudeConfig::forget()). Captured once at startup
// and shown on the lock screen itself, since serial capture on this
// hardware has proven too unreliable (auto-reset-on-connect eats the
// boot-time window) to rely on catching this live over USB.
String resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external reset";
    case ESP_RST_SW: return "firmware reboot";  // e.g. after an OTA update
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT: return "watchdog (interrupt)";
    case ESP_RST_TASK_WDT: return "watchdog (task)";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "brownout (power dip)";
    case ESP_RST_SDIO: return "SDIO";
    default: return "unknown";
  }
}
String bootReason;

// 2026-08-15: two live serial captures (both full symbolicated
// backtraces, see CHANGELOG.md) caught abort() firing inside ESP-IDF's
// lock_init_generic (newlib/locks.c) - the actual source shows the only
// abort there is "xQueueCreateMutex() returned NULL", i.e. a semaphore
// allocation failed - reached via LWIP's own internal tcpip_thread
// logging a routine mDNS UDP-packet message for the very first time.
// newlib gives each FreeRTOS task its own lazily-created stdio lock on
// first use - tcpip_thread had never logged anything before, so this was
// its first-ever attempt, and free-heap logging (checkFetchResult()
// below) showed the heap completely flat for 3h43m beforehand, ruling
// out gradual fragmentation. Both crashes landed within seconds of a
// fresh ClaudeUsageClient::fetch() WiFiClientSecure TLS handshake
// starting - mbedTLS's large transient handshake buffers are exactly the
// kind of momentary heap pressure that can starve an unrelated task's
// small allocation for the split second it needs one. Muting mDNS's own
// logging (see esp_log_level_set() in begin()) didn't stop the second
// crash - same PC, same backtrace - most likely because that specific
// log call is gated by a compile-time LOG_LOCAL_LEVEL rather than the
// runtime level esp_log_level_set() controls, so it fires regardless.
//
// Fix: force tcpip_thread's first-ever libc/stdio call (and therefore
// its lock's lazy init) to happen right here, scheduled via LWIP's own
// tcpip_callback() so it runs *on* that thread - on the still-pristine
// heap right after WiFi connects, long before the first TLS handshake
// (called from onWifiConnected() below, before pollUsage(true)). Once
// created, the lock is just reused forever after; this doesn't fix
// TLS's transient heap pressure, only removes the one confirmed casualty
// of it. printf(), not Serial.print() - Serial's HardwareSerial driver
// writes the UART directly and never touches this lock at all; only
// libc/ESP_LOGx output (through the console VFS) does.
void tcpipThreadWarmupCb(void *) { printf("[lwip] tcpip_thread warmed up\n"); }

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
  // "Next update in M:SS" was wide enough to wrap inside the countdown
  // label's fixed-width box on the small displays (CYD/D1 mini) - "Next:"
  // keeps the same meaning in about half the characters.
  snprintf(buf, sizeof(buf), "Next: %lu:%02lu", remainingSec / 60, remainingSec % 60);
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

// --- System Info overlay -------------------------------------------------
// Reached via a short press of the physical BOOT button (see
// pollBootButton() below) - not part of the normal touch UI, since a
// device with a broken touch digitizer (see the earlier one-off "shop2"
// build) can still reach diagnostics this way, and it doubles as a quick
// way to check WiFi signal/heap/uptime without a browser or serial cable.
// Same overlay pattern as openSettingsOverlay() above, deliberately left
// scrollable (unlike the main dashboard's root) as a safety net in case
// these rows ever don't fit CYD/D1 mini's 240px height the way the status
// row briefly didn't - see the 2026-08-03 layout fix.

void closeSysInfoOverlay() {
  if (sysInfoOverlay) {
    lv_obj_del(sysInfoOverlay);
    sysInfoOverlay = nullptr;
    sysInfoIpVal = sysInfoRssiVal = sysInfoHeapVal = sysInfoUptimeVal = sysInfoAccountVal = nullptr;
  }
}

void onCloseSysInfoClicked(lv_event_t *e) { closeSysInfoOverlay(); }

void renderSystemInfoValues() {
  if (!sysInfoOverlay) return;

  lv_label_set_text_fmt(sysInfoIpVal, "IP: %s", deviceIp.length() > 0 ? deviceIp.c_str() : "-");

  if (WiFi.status() == WL_CONNECTED) {
    lv_label_set_text_fmt(sysInfoRssiVal, "Wi-Fi signal: %d dBm", WiFi.RSSI());
  } else {
    lv_label_set_text(sysInfoRssiVal, "Wi-Fi signal: disconnected");
  }

  lv_label_set_text_fmt(sysInfoHeapVal, "Free heap: %u KB (min block %u KB)",
                         (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));

  lv_label_set_text_fmt(sysInfoUptimeVal, "Uptime: %s", formatUptime(millis() / 1000).c_str());

  const char *acctStatus = !ClaudeConfig::isProvisioned() ? "Not configured"
                            : ClaudeConfig::isUnlocked()  ? "Unlocked"
                                                           : "Locked";
  lv_label_set_text_fmt(sysInfoAccountVal, "Account: %s", acctStatus);
}

// Called every loop() iteration, throttled to ~1s - mirrors tickLiveStatus()
// above. A no-op (single flag check) whenever the overlay isn't open.
void tickSystemInfo() {
  if (!sysInfoOverlay) return;
  static unsigned long lastTickMs = 0;
  unsigned long now = millis();
  if (now - lastTickMs < 1000) return;
  lastTickMs = now;
  renderSystemInfoValues();
}

void openSysInfoOverlay() {
  if (sysInfoOverlay) return;  // already open - a second short press is a harmless no-op

  sysInfoOverlay = lv_obj_create(lv_scr_act());
  lv_obj_add_flag(sysInfoOverlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(sysInfoOverlay, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(sysInfoOverlay, 0, 0);
  lv_obj_set_style_bg_color(sysInfoOverlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(sysInfoOverlay, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(sysInfoOverlay, 10, 0);
  lv_obj_set_flex_flow(sysInfoOverlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(sysInfoOverlay, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(sysInfoOverlay, 6, 0);

  lv_obj_t *title = lv_label_create(sysInfoOverlay);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_text(title, "System Info");
  lv_obj_set_style_pad_bottom(title, 6, 0);

  auto makeRow = [&]() -> lv_obj_t * {
    lv_obj_t *row = lv_label_create(sysInfoOverlay);
    lv_obj_set_style_text_color(row, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(row, &lv_font_montserrat_14, 0);
    return row;
  };

  lv_obj_t *fwRow = makeRow();
  lv_label_set_text_fmt(fwRow, "Firmware: v%s", FW_VERSION);

  sysInfoIpVal = makeRow();
  sysInfoRssiVal = makeRow();
  sysInfoHeapVal = makeRow();
  sysInfoUptimeVal = makeRow();
  sysInfoAccountVal = makeRow();

  lv_obj_t *bootRow = makeRow();
  lv_label_set_text_fmt(bootRow, "Last boot: %s", bootReason.c_str());

  lv_obj_t *closeBtn = lv_btn_create(sysInfoOverlay);
  lv_obj_set_size(closeBtn, lv_pct(80), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(closeBtn, LV_OPA_20, 0);
  lv_obj_set_style_bg_color(closeBtn, lv_color_white(), 0);
  lv_obj_set_style_pad_top(closeBtn, 10, 0);
  lv_obj_add_event_cb(closeBtn, onCloseSysInfoClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *closeLabel = lv_label_create(closeBtn);
  lv_label_set_text(closeLabel, "Close");
  lv_obj_set_style_text_color(closeLabel, lv_color_white(), 0);
  lv_obj_center(closeLabel);

  renderSystemInfoValues();  // populate immediately instead of waiting up to 1s for the first tick
}

// Wipes WiFi credentials *and* the Claude account, then reboots - the
// on-device equivalent of the Settings overlay's "Forget Wi-Fi" +
// "Reset Claude Account" buttons combined into one action, since a unit
// whose touch is too broken to reach either button (see the "shop2"
// one-off) still has a working BOOT button.
void performFactoryReset() {
  Serial.printf("[app] boot button held %lu ms, factory resetting\n", FACTORY_RESET_HOLD_MS);
  ClaudeConfig::forget();
  TouchWifiProvisioner::reset();
  Serial.flush();
  delay(100);
  ESP.restart();
}

// Called every loop() iteration - digitalRead() is cheap enough not to need
// its own throttle. Edge-triggers openSysInfoOverlay() on press, then
// watches the same press for a long hold to fire performFactoryReset().
void pollBootButton() {
  static bool wasPressed = false;
  static unsigned long pressedAtMs = 0;
  static bool resetFired = false;

  const bool pressed = digitalRead(BOOT_BUTTON_PIN) == LOW;  // active-low
  if (pressed && !wasPressed) {
    openSysInfoOverlay();
    pressedAtMs = millis();
    resetFired = false;
  } else if (pressed && !resetFired && millis() - pressedAtMs >= FACTORY_RESET_HOLD_MS) {
    resetFired = true;
    performFactoryReset();
  }
  wasPressed = pressed;
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
  // Diagnostic for the 2026-08-07 crash investigation: a fresh
  // WiFiClientSecure+HTTPClient is created and torn down every poll (see
  // ClaudeUsageClient::fetch()'s comment on why a fresh TLS connection is
  // needed per attempt), and repeated large mbedTLS alloc/free cycles are
  // a known way to fragment the ESP32 heap over many hours even without an
  // outright leak. getFreeHeap() alone can look fine while getMaxAllocHeap()
  // (largest single free block) quietly shrinks - logging both makes that
  // distinction visible in hindsight if a crash follows.
  Serial.printf("[app] heap: free=%u maxAlloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
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
    UsageDashboard::setStatusLine("Locked (" + bootReason + ") - visit " + setupUrls() + " to unlock");
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
  bootReason = resetReasonText();
  Serial.printf("[boot] reset reason: %s\n", bootReason.c_str());

  // 2026-08-07 crash: full backtrace traced an abort() in ESP-IDF's
  // lock_init_generic (newlib/locks.c) back through esp_log_write, called
  // from the vendored mDNS component's own internal logging inside
  // _udp_recv (mdns_networking_lwip.c) on the LWIP tcpip_thread - i.e. an
  // unrelated background thread's routine debug/info log about a UDP
  // packet, at the exact moment its first-ever printf/UART-lock lazy-init
  // landed on a heap too fragmented (see the heap logging in
  // checkFetchResult() above) to allocate the lock's semaphore. This mutes
  // ESP-IDF's own internal component logs (WiFi/mDNS/LWIP/etc.) - none of
  // this project's own Serial.printf output goes through esp_log, so
  // nothing here is lost - which removes that exact trigger regardless of
  // whether the heap-fragmentation theory is the whole story.
  esp_log_level_set("*", ESP_LOG_WARN);

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

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

  // Only needs to happen once, ever - see tcpipThreadWarmupCb()'s comment.
  // tcpip_thread is guaranteed running by this point (WiFi just connected),
  // and nothing below has touched TLS yet.
  static bool tcpipWarmedUp = false;
  if (!tcpipWarmedUp) {
    tcpipWarmedUp = true;
    tcpip_callback(tcpipThreadWarmupCb, nullptr);
  }

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
  pollBootButton();
  tickSystemInfo();
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
