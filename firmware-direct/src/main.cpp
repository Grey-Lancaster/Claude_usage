// Claude_usage - firmware-direct
//
// Cheap Yellow Display (ESP32-2432S028R) dashboard showing your live
// Claude.ai session/weekly plan-limit percentages and usage credits.
// Talks straight to claude.ai over HTTPS. The session cookie is stored
// encrypted (AES-256-GCM, key derived from a passphrase you choose - see
// ClaudeConfig.h) and set up via a tiny on-device web page at
// http://claudeusage.local/ (see ClaudeSetupServer.h) rather than typed
// into the on-screen keyboard. Read ../README.md before deploying this
// on a device that could walk off - if you'd rather the cookie never
// touch the device at all, use ../../firmware-relay instead.
//
// Display/touch bring-up below is copied from TouchWifiProvisioner's
// CYD_BasicConnect example - that library never touches the display bus
// or touch controller directly, so this section is what you'd swap for a
// different board.

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include <ezTime.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>  // version 8.3.11
#include <TouchWifiProvisioner.h>

#include "ClaudeConfig.h"
#include "ClaudeSetupServer.h"
#include "ClaudeUsageClient.h"
#include "OtaPassword.h"
#include "UsageDashboard.h"

// waitForSync()/events() live in namespace ezt, not global scope.
using namespace ezt;

// Drives both the display's "resets in"-as-absolute-time formatting
// (ClaudeUsageClient reads this via ezTime's default-timezone bounce-
// through functions, set via setDefault() below) and is re-pointed live
// when the timezone is changed from the setup page.
Timezone myTZ;

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800

static const uint16_t SCREEN_W = 320;
static const uint16_t SCREEN_H = 240;
static const unsigned long POLL_INTERVAL_MS = 5UL * 60UL * 1000UL;  // claude.ai is polled sparingly - this is an undocumented endpoint, not a rate-limit-friendly public API

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_W * 40];

static lv_obj_t *settingsOverlay = nullptr;
static unsigned long lastPollMs = 0;
static bool dashboardReady = false;

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  if (!ts.touched()) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }
  TS_Point p = ts.getPoint();
  data->state = LV_INDEV_STATE_PR;
  data->point.x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W);
  data->point.y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H);
}

static void closeSettingsOverlay() {
  if (settingsOverlay) {
    lv_obj_del(settingsOverlay);
    settingsOverlay = nullptr;
  }
}

static void onForgetWifiClicked(lv_event_t *e) {
  closeSettingsOverlay();
  TouchWifiProvisioner::reset();
}

static void onResetAccountClicked(lv_event_t *e) {
  closeSettingsOverlay();
  ClaudeConfig::forget();
  UsageDashboard::setStatusLine("Not configured - visit http://claudeusage.local/");
}

static void onCloseSettingsClicked(lv_event_t *e) { closeSettingsOverlay(); }

static void openSettingsOverlay() {
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
  lv_label_set_text(urlLabel, "Setup: http://claudeusage.local/");

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
static portMUX_TYPE fetchMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool fetchRequested = false;
static volatile bool fetchBusy = false;
static String pendingOrgId;
static String pendingCookie;
static volatile bool fetchResultReady = false;
static UsageDashboard::Snapshot fetchResultSnap;
static bool fetchResultOk = false;

static void fetchTaskFn(void *) {
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

// Drains a completed background fetch, if any, onto the dashboard. Called
// every loop() iteration - cheap (a flag check) when nothing's ready.
static void checkFetchResult() {
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
  Serial.printf("[main] pollUsage: fetch ok=%d snap.valid=%d snap.error='%s'\n", ok, snap.valid, snap.error.c_str());
  UsageDashboard::update(snap);
  ClaudeSetupServer::setLastFetchStatus(ok, snap.error);
  if (ok) {
    UsageDashboard::setStatusLine("Updated " + String(millis() / 1000) + "s uptime");
  }
}

static void pollUsage(bool force) {
  unsigned long now = millis();
  if (!force && now - lastPollMs < POLL_INTERVAL_MS) return;
  lastPollMs = now;

  if (!ClaudeConfig::isProvisioned()) {
    UsageDashboard::setStatusLine("Not configured - visit http://claudeusage.local/");
    return;
  }
  if (!ClaudeConfig::isUnlocked()) {
    UsageDashboard::setStatusLine("Locked - visit http://claudeusage.local/ to unlock");
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
static void manualRefresh() {
  static unsigned long lastManualMs = 0;
  unsigned long now = millis();
  if (now - lastManualMs < 3000) return;
  lastManualMs = now;
  pollUsage(true);
}

static void onWifiConnected(const String &ip) {
  Serial.printf("Wi-Fi connected, IP: %s\n", ip.c_str());
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
    ClaudeSetupServer::begin();

    // Network OTA - "pio run -e cyd_ota -t upload" instead of USB once the
    // device is on Wi-Fi. Doesn't touch the partition table (already
    // OTA-shaped via min_spiffs.csv), so this can't shift NVS and forget
    // saved Wi-Fi/account data the way flashing a build with a *different*
    // partition table would.
    ArduinoOTA.setHostname("claudeusage");
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
  }

  pollUsage(true);
}

void setup() {
  Serial.begin(115200);
  ClaudeConfig::begin();

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  SPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin();
  ts.setRotation(3);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, SCREEN_W * 40);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read;
  lv_indev_drv_register(&indev_drv);

  TouchWifiProvisioner::begin(lv_scr_act(), "ClaudeUsage", onWifiConnected);

  // Pinned to core 0 - Arduino's setup()/loop() (and therefore
  // lv_timer_handler()) run as "loopTask" on core 1 by default, so this
  // keeps the network fetch fully off the UI's core.
  xTaskCreatePinnedToCore(fetchTaskFn, "usageFetch", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  lv_timer_handler();
  checkFetchResult();
  TouchWifiProvisioner::loop();
  if (TouchWifiProvisioner::isConnected()) {
    events();  // services ezTime's background NTP re-sync scheduling
    ClaudeSetupServer::handleClient();
    ArduinoOTA.handle();
    pollUsage(false);
  }
  delay(5);
}
