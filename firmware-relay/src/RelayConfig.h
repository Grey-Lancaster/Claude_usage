// Relay server address (host:port only, e.g. "192.168.1.50:8787") - short
// enough to type on the on-screen keyboard, unlike a session cookie.
// Persisted via Preferences, same mechanism TouchWifiProvisioner uses for
// Wi-Fi credentials.
#pragma once

#include <Arduino.h>

namespace RelayConfig {

void begin();

bool isConfigured();
String hostPort();
void setHostPort(const String &value);

} // namespace RelayConfig
