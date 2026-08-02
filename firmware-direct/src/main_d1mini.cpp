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
#include "ScreenshotCapture.h"
#include "boot_logo_cyd.h"

// Same physical bus as the display (see platformio.ini's TFT_MISO/MOSI/
// SCLK) - only chip-select differs. No IRQ pin: neither Wemos's own
// reference sketch for this shield family (both its D1-mini and D32 Pro
// pin presets) nor the shield's official pin table wire one up - touch
// works fine without it since XPT2046_Touchscreen::touched() does its
// own SPI pressure read regardless (see XPT2046_Touchscreen.cpp's
// update()), it just can't use the interrupt-driven fast path. An
// earlier version of this file added GPIO16 as IRQ based on the
// commented-out (never live) touchscreen block in esp32tft3.yaml -
// removed now that the manufacturer's own examples contradict it.
#define XPT2046_CS 12

// Measured directly off the physical panel via runCalibrationScreen()
// (see below) - raw readings were top-left (3797, 397), top-right
// (384, 474), bottom-right (422, 3765). X is inverted relative to
// screen orientation (raw decreases left-to-right); Y is not. Values
// below pad ~100 past the measured extremes so a touch right at the
// physical edge doesn't fall just outside the calibrated range -
// touchpad_read() also clamps the final result as a backstop.
#define TOUCH_X_MIN 3900  // raw at screen x=0 (left edge)
#define TOUCH_X_MAX 300   // raw at screen x=SCREEN_W (right edge)
#define TOUCH_Y_MIN 350   // raw at screen y=0 (top edge)
#define TOUCH_Y_MAX 3850  // raw at screen y=SCREEN_H (bottom edge)

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
  // Clamped as a backstop - map() alone can extrapolate past 0/SCREEN_W-1
  // for a touch right at (or just past) the calibrated physical edge.
  data->point.x = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W), 0, SCREEN_W - 1);
  data->point.y = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H), 0, SCREEN_H - 1);
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

  AppLogic::setScreenshotHandler([](WebServer &server) { ScreenshotCapture::stream(server, SCREEN_W, SCREEN_H); });
  AppLogic::begin();
  TouchWifiProvisioner::begin(lv_scr_act(), "ClaudeUsage-D1", AppLogic::onWifiConnected);
}

void loop() {
  lv_timer_handler();
  AppLogic::loop();
}
