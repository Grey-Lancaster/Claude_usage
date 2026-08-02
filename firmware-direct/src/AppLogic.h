// Board-independent app orchestration, shared by every board's main file
// (main.cpp for the CYD, main_crowpanel7.cpp for the CrowPanel Advance
// 7"). Everything here only touches LVGL objects, ClaudeConfig,
// ClaudeSetupServer, ClaudeUsageClient, and ArduinoOTA/WiFi - nothing
// board-specific (display bus, touch controller, pin numbers). A board's
// main file is responsible for exactly one thing this module can't do
// itself: getting a working LVGL display + touch input registered before
// calling AppLogic::begin().
#pragma once

#include <Arduino.h>

namespace AppLogic {

// Call once from setup(), after ClaudeConfig::begin() and LVGL's
// display/indev drivers are registered, but before
// TouchWifiProvisioner::begin(). Starts the background usage-fetch task.
void begin();

// Pass this as TouchWifiProvisioner::begin()'s callback.
void onWifiConnected(const String &ip);

// Call every loop() iteration, right after lv_timer_handler().
void loop();

} // namespace AppLogic
