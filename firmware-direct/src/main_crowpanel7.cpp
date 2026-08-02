// Claude_usage - firmware-direct (Elecrow CrowPanel Advance 7.0" HMI)
//
// Board bring-up only - see main.cpp's header comment for what that
// means and what this dashboard actually does. This file exists because
// the CrowPanel is a genuinely different display/touch/PSRAM stack from
// the CYD (RGB parallel bus + LovyanGFX instead of SPI + TFT_eSPI, GT911
// capacitive touch over I2C instead of XPT2046 resistive touch over SPI),
// not just a pin remap - see TouchWifiProvisioner's
// CrowPanel7_RollingClock example, which this bring-up section is copied
// from, for the reference this was built against.

#include <Arduino.h>
#include <lvgl.h>  // version 8.3.11
#include <Wire.h>
#include <WiFi.h>
#include <TouchWifiProvisioner.h>

#include "AppLogic.h"
#include "ClaudeConfig.h"
#include "LovyanGFX_Driver_CrowPanel7.h"
#include "ScreenshotCapture.h"
#include "boot_logo_crowpanel7.h"

#define LCD_H_RES 800
#define LCD_V_RES 480

LGFX gfx;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;
static lv_color_t *buf2;
static uint16_t touch_x, touch_y;

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  if (gfx.getStartCount() > 0) {
    gfx.endWrite();
  }
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
  // Safe to read color_p here even though the DMA push above may still be
  // in flight - this only reads the buffer, never writes it, so there's
  // no race with the DMA engine's own (also read-only) access.
  ScreenshotCapture::feedArea(area, color_p, w, h);
  lv_disp_flush_ready(disp);
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  data->state = LV_INDEV_STATE_REL;
  if (gfx.getTouch(&touch_x, &touch_y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch_x;
    data->point.y = touch_y;
  }
}

// The CrowPanel's onboard STC8H1K28 microcontroller gates the backlight and
// arms the GT911 touch controller over I2C - without this, the panel stays
// dark and touch never responds, regardless of how LovyanGFX/LVGL are set up.
static bool i2cScanForAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

static void sendI2CCommand(uint8_t command) {
  Wire.beginTransmission(0x30);
  Wire.write(command);
  Wire.endTransmission();
}

// Boot splash: drawn directly via LovyanGFX (not LVGL) so it renders
// before any LVGL init overhead - copied verbatim from
// TouchWifiProvisioner's own DBN One Page project, same board/driver,
// same logo asset (boot_logo_crowpanel7.h/.c, generated from that
// project's source PNG).
static void drawBootSplash() {
  gfx.fillScreen(TFT_WHITE);  // matches the logo's own white card background
  gfx.pushImage((LCD_H_RES - CROWPANEL7_BOOT_LOGO_SIZE) / 2, (LCD_V_RES - CROWPANEL7_BOOT_LOGO_SIZE) / 2,
                CROWPANEL7_BOOT_LOGO_SIZE, CROWPANEL7_BOOT_LOGO_SIZE, (lgfx::rgb565_t *)crowpanel7_boot_logo_map);
  delay(5000);
  gfx.fillScreen(TFT_BLACK);
}

void setup() {
  Serial.begin(115200);
  ClaudeConfig::begin();

  Wire.begin(15, 16);
  delay(50);
  while (!(i2cScanForAddress(0x30) && i2cScanForAddress(0x5D))) {
    Serial.println("Waiting for backlight/touch controller...");
    sendI2CCommand(250); // activate touch screen
    pinMode(1, OUTPUT);
    digitalWrite(1, LOW);
    delay(120);
    pinMode(1, INPUT);
    delay(100);
  }
  sendI2CCommand(0); // brightest backlight (0-245, 245 = off)

  gfx.init();
  gfx.initDMA();
  gfx.startWrite();
  drawBootSplash();

  lv_init();

  size_t buffer_size = sizeof(lv_color_t) * LCD_H_RES * LCD_V_RES;
  buf1 = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
  buf2 = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_H_RES * LCD_V_RES);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_H_RES;
  disp_drv.ver_res = LCD_V_RES;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read;
  lv_indev_drv_register(&indev_drv);

  AppLogic::setScreenshotHandler([](WebServer &server) { ScreenshotCapture::stream(server, LCD_H_RES, LCD_V_RES); });
  AppLogic::begin();
  TouchWifiProvisioner::begin(lv_scr_act(), "ClaudeUsage-CP7", AppLogic::onWifiConnected);
}

void loop() {
  lv_timer_handler();
  AppLogic::loop();
}
