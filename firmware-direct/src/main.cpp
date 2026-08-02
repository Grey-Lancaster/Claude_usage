// Claude_usage - firmware-direct (Cheap Yellow Display / ESP32-2432S028R)
//
// Board bring-up only - this file's job is getting a working LVGL
// display + touch input registered, then handing off to AppLogic (shared
// with every other board this project supports, e.g. the CrowPanel
// Advance 7" in main_crowpanel7.cpp). See ../README.md for what this
// dashboard actually does.
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

#include "AppLogic.h"
#include "ClaudeConfig.h"
#include "ScreenshotCapture.h"
#include "boot_logo_cyd.h"

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

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_W * 40];

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  ScreenshotCapture::feedArea(area, color_p, w, h);
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

// Boot splash: same pushColors(..., swap=true) call disp_flush() already
// uses for every LVGL frame on this exact panel, just aimed at a raw logo
// buffer instead of LVGL's draw buffer - reuses a proven-correct call
// rather than a second, separately-verified byte-order convention.
// cyd_boot_logo_map (common/boot_logo_cyd.c) was generated from
// TouchWifiProvisioner's DBN One Page project's source logo PNG, resized
// for this screen.
static void drawBootSplash() {
  tft.fillScreen(TFT_WHITE);  // matches the logo's own white card background
  int x = (SCREEN_W - CYD_BOOT_LOGO_SIZE) / 2;
  int y = (SCREEN_H - CYD_BOOT_LOGO_SIZE) / 2;
  tft.startWrite();
  tft.setAddrWindow(x, y, CYD_BOOT_LOGO_SIZE, CYD_BOOT_LOGO_SIZE);
  tft.pushColors((uint16_t *)cyd_boot_logo_map, CYD_BOOT_LOGO_SIZE * CYD_BOOT_LOGO_SIZE, true);
  tft.endWrite();
  delay(5000);
  tft.fillScreen(TFT_BLACK);
}

void setup() {
  Serial.begin(115200);
  ClaudeConfig::begin();

  tft.init();
  tft.setRotation(3);
  drawBootSplash();

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

  AppLogic::setScreenshotHandler([](WebServer &server) { ScreenshotCapture::stream(server, SCREEN_W, SCREEN_H); });
  AppLogic::begin();
  TouchWifiProvisioner::begin(lv_scr_act(), "ClaudeUsage", AppLogic::onWifiConnected);
}

void loop() {
  lv_timer_handler();
  AppLogic::loop();
}
