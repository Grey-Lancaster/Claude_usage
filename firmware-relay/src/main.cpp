// Claude_usage - firmware-relay
//
// Cheap Yellow Display (ESP32-2432S028R) dashboard showing your live
// Claude.ai session/weekly plan-limit percentages and usage credits.
// Unlike ../../firmware-direct, this variant never holds your session
// cookie - it polls a small relay server (see ../../relay-server) running
// on your PC over plain local-network HTTP. If that PC isn't always on,
// use firmware-direct instead; see the root README for the tradeoff.
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

#include "RelayClient.h"
#include "RelayConfig.h"
#include "RelaySettings.h"
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
static const unsigned long POLL_INTERVAL_MS = 60UL * 1000UL;  // local LAN hop to the relay - cheap enough to poll often

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

static void pollUsage(bool force) {
  unsigned long now = millis();
  if (!force && now - lastPollMs < POLL_INTERVAL_MS) return;
  lastPollMs = now;

  if (!RelayConfig::isConfigured()) {
    UsageDashboard::setStatusLine("Not configured - set relay address in Settings");
    return;
  }

  UsageDashboard::Snapshot snap;
  bool ok = RelayClient::fetch(RelayConfig::hostPort(), snap);
  UsageDashboard::update(snap);
  if (ok) {
    UsageDashboard::setStatusLine("Updated " + String(now / 1000) + "s uptime");
  }
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

static void onChangeRelayClicked(lv_event_t *e) {
  closeSettingsOverlay();
  RelaySettings::open(lv_scr_act(), [](const String &) { pollUsage(true); });
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

  lv_obj_t *forgetBtn = lv_btn_create(settingsOverlay);
  lv_obj_set_size(forgetBtn, lv_pct(80), LV_SIZE_CONTENT);
  lv_obj_add_event_cb(forgetBtn, onForgetWifiClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *forgetLabel = lv_label_create(forgetBtn);
  lv_label_set_text(forgetLabel, "Forget Wi-Fi");
  lv_obj_center(forgetLabel);

  lv_obj_t *relayBtn = lv_btn_create(settingsOverlay);
  lv_obj_set_size(relayBtn, lv_pct(80), LV_SIZE_CONTENT);
  lv_obj_add_event_cb(relayBtn, onChangeRelayClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *relayLabel = lv_label_create(relayBtn);
  lv_label_set_text(relayLabel, "Change Relay Address");
  lv_obj_center(relayLabel);

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

static void onWifiConnected(const String &ip) {
  Serial.printf("Wi-Fi connected, IP: %s\n", ip.c_str());

  // TouchWifiProvisioner fires this callback again after every reconnect,
  // not just the first connection - the dashboard only needs building
  // once; a reconnect should just let polling resume.
  if (!dashboardReady) {
    dashboardReady = true;
    UsageDashboard::build(lv_scr_act());
    UsageDashboard::setOnSettingsClicked(openSettingsOverlay);

    if (!RelayConfig::isConfigured()) {
      RelaySettings::open(lv_scr_act(), [](const String &) { pollUsage(true); });
      UsageDashboard::setStatusLine("Not configured - set relay address");
      return;
    }
  }

  if (RelayConfig::isConfigured()) pollUsage(true);
}

void setup() {
  Serial.begin(115200);
  RelayConfig::begin();

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
  if (TouchWifiProvisioner::isConnected()) pollUsage(false);
  delay(5);
}
