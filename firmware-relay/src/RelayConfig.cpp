#include "RelayConfig.h"

#include <Preferences.h>

namespace RelayConfig {
namespace {

const char *PREFS_NAMESPACE = "relaycfg";
Preferences prefs;
String cachedHostPort;

} // namespace

void begin() {
  prefs.begin(PREFS_NAMESPACE, true);
  cachedHostPort = prefs.getString("hostport", "");
  prefs.end();
}

bool isConfigured() { return cachedHostPort.length() > 0; }
String hostPort() { return cachedHostPort; }

void setHostPort(const String &value) {
  cachedHostPort = value;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString("hostport", value);
  prefs.end();
}

} // namespace RelayConfig
