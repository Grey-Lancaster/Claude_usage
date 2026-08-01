// Shared by main.cpp (ArduinoOTA) and ClaudeSetupServer.cpp (the browser
// firmware-upload page) so both update paths use the same password.
// Override via platformio.ini's -DOTA_PASSWORD=\"...\" (see cyd_ota env) -
// CHANGE this before relying on either update path; "changeme" left as-is
// means anyone on your LAN who finds the device can push firmware to it.
#pragma once

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "changeme"
#endif
