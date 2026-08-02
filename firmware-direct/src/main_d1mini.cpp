// Claude_usage - firmware-direct (Wemos/LOLIN D1 mini + TFT 2.4" Touch
// Shield V1.0, ESP32 clone of the D1 mini form factor - specifically an
// MH-ET LIVE MiniKit)
//
// Board bring-up only - see main.cpp's header comment for what that means
// and what this dashboard actually does. Same TFT_eSPI/XPT2046 stack as
// the CYD, but wired very differently: this shield puts the TFT and the
// touch controller on the *same* physical SPI bus (shared SCK/MOSI/MISO,
// separate CS lines) rather than two independent buses.
//
// Every pin below (including TFT_MISO/MOSI/SCLK/CS/DC/RST - see
// platformio.ini's [env:d1mini]) is confirmed from this exact physical
// device's prior ESPHome config (esp32tft3.yaml), which the user
// rediscovered after this port was first written from best-available
// public docs alone - those docs got the SPI bus and TFT_CS/TFT_DC right
// but had no way to know TFT_RST was a real pin (not tied to board reset)
// or where the touch CS/IRQ lines landed.
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
// SCLK) - only chip-select differs.
#define XPT2046_CS 12
#define XPT2046_IRQ 16

// esp32tft3.yaml's touchscreen block (source of XPT2046_CS/IRQ above) was
// entirely commented out, so touch was never actually exercised on this
// device - and its calibration values turned out to be ESPHome's own
// xpt2046 doc example almost verbatim (x_max/y_max identical to the doc,
// mins rounded to 0 rather than the doc's 280/340), not a real on-device
// calibration pass. Left as a placeholder starting point - expect to
// redo this for real once the hardware's in hand: read raw ts.getPoint()
// values over Serial while touching each corner of the screen, then
// replace these four with what you actually measure.
#define TOUCH_X_MIN 0
#define TOUCH_X_MAX 3860
#define TOUCH_Y_MIN 0
#define TOUCH_Y_MAX 3860

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
