// Claude_usage - firmware-direct (Wemos/LOLIN D1 mini + TFT 2.4" Touch
// Shield V1.0, ESP32 clone of the D1 mini form factor)
//
// Board bring-up only - see main.cpp's header comment for what that means
// and what this dashboard actually does. Same TFT_eSPI/XPT2046 stack as
// the CYD, but wired very differently: this shield puts the TFT and the
// touch controller on the *same* physical SPI bus (shared SCK/MOSI/MISO,
// separate CS lines) rather than two independent buses, and its default
// jumpering (per the shield's own docs - see platformio.ini's [env:d1mini]
// comment) leaves the touch IRQ line and the backlight both unconnected.
//
// PIN MAPPING CAVEAT: the "D1 mini ESP32" clone family (MH-ET LIVE MiniKit
// and its many rebrands) is not pin-identical across manufacturers - only
// the SPI-bus pins (D5/D6/D7 -> GPIO18/19/23, matching the ESP32's own
// hardware VSPI defaults, which is exactly why these clones exist) are
// consistently documented everywhere. The D0/D3/D8 assignments below
// (TFT_CS/TS_CS/TFT_DC) are this project's best-available mapping,
// cross-checked against multiple independent sources but NOT verified
// against this specific physical board yet. If the display stays blank
// or touch doesn't register once this is actually flashed and tested,
// start here - see platformio.ini for the full build_flags.
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>  // version 8.3.11
#include <TouchWifiProvisioner.h>

#include "AppLogic.h"
#include "ClaudeConfig.h"
#include "boot_logo_cyd.h"

// Same physical bus as the display (see platformio.ini's TFT_MISO/MOSI/
// SCLK) - only the touch chip-select is a separate pin. No IRQ line is
// broken out on this shield by default, so touches are read via SPI
// polling instead of an interrupt (XPT2046_Touchscreen supports this -
// just construct it with only a CS pin).
#define XPT2046_CS 17  // D3

#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800

static const uint16_t SCREEN_W = 320;
static const uint16_t SCREEN_H = 240;

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(XPT2046_CS);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_W * 40];

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

// Boot splash: same pushColors(..., swap=true) call disp_flush() already
// uses for every LVGL frame on this exact panel family, just aimed at a
// raw logo buffer instead of LVGL's draw buffer. Reuses the CYD's logo
// asset verbatim (common/boot_logo_cyd.c) rather than generating a new
// one - this shield's panel is the same 320x240 resolution/orientation.
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

  // No separate SPI.begin() call for touch - TFT_eSPI's tft.init() above
  // already brought up the default VSPI bus on the same GPIO18/19/23 pins
  // this shield ties the touch controller to, and XPT2046_Touchscreen
  // reuses that same global SPI instance.
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

  AppLogic::begin();
  TouchWifiProvisioner::begin(lv_scr_act(), "ClaudeUsage-D1", AppLogic::onWifiConnected);
}

void loop() {
  lv_timer_handler();
  AppLogic::loop();
}
