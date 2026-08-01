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
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>  // version 8.3.11
#include <TouchWifiProvisioner.h>

#include "ClaudeConfig.h"
#include "ClaudeSetupServer.h"
#include "ClaudeUsageClient.h"
#include "UsageDashboard.h"

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

  UsageDashboard::Snapshot snap;
  bool ok = ClaudeUsageClient::fetch(ClaudeConfig::orgId(), ClaudeConfig::cookie(), snap);
  UsageDashboard::update(snap);
  if (ok) {
    UsageDashboard::setStatusLine("Updated " + String(now / 1000) + "s uptime");
  }
}

static void onWifiConnected(const String &ip) {
  Serial.printf("Wi-Fi connected, IP: %s\n", ip.c_str());
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // TouchWifiProvisioner fires this callback again after every reconnect,
  // not just the first connection - the dashboard and web server only
  // need starting once; a reconnect should just let polling resume.
  if (!dashboardReady) {
    dashboardReady = true;
    UsageDashboard::build(lv_scr_act());
    UsageDashboard::setOnSettingsClicked(openSettingsOverlay);

    ClaudeSetupServer::setOnUnlocked([]() { pollUsage(true); });
    ClaudeSetupServer::begin();
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
}

void loop() {
  lv_timer_handler();
  TouchWifiProvisioner::loop();
  if (TouchWifiProvisioner::isConnected()) {
    ClaudeSetupServer::handleClient();
    pollUsage(false);
  }
  delay(5);
}
