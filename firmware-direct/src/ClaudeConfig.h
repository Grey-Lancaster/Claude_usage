// Org ID + session cookie, provisioned over USB serial rather than the
// on-screen keyboard - a session cookie runs 100+ characters, which is
// painful to peck out on a resistive touchscreen but trivial to paste
// into a serial terminal. Persisted via Preferences, same mechanism
// TouchWifiProvisioner uses for Wi-Fi credentials.
#pragma once

#include <Arduino.h>

namespace ClaudeConfig {

void begin();

bool isConfigured();
String orgId();
String cookie();

// Starts (or restarts) the "paste your org id, then your cookie" serial
// prompt. Safe to call while already armed.
void startSetup();

// Call every loop() iteration - non-blocking line reader that drives the
// setup prompt when armed. No-op otherwise.
void pollSerial();

void clear();

} // namespace ClaudeConfig
