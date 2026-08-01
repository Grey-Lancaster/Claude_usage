#include "RelayClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

namespace RelayClient {

bool fetch(const String &hostPort, UsageDashboard::Snapshot &out) {
  out.valid = false;

  WiFiClient client;
  HTTPClient http;
  String url = "http://" + hostPort + "/usage";
  if (!http.begin(client, url)) {
    out.error = "HTTP begin failed";
    return false;
  }
  http.setTimeout(5000);

  int code = http.GET();
  if (code <= 0) {
    http.end();
    out.error = "Relay unreachable";
    return false;
  }

  DynamicJsonDocument doc(768);
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    out.error = "JSON parse: " + String(err.c_str());
    return false;
  }

  bool ok = doc["ok"] | false;
  if (!ok) {
    const char *relayError = doc["error"] | "Relay has no data yet";
    out.error = relayError;
    return false;
  }

  out.sessionPercent = doc["session_percent"] | 0;
  out.sessionResetsIn = (const char *)(doc["session_resets_in"] | "?");
  out.weeklyPercent = doc["weekly_percent"] | 0;
  out.weeklyResetsIn = (const char *)(doc["weekly_resets_in"] | "?");
  out.creditsEnabled = doc["credits_enabled"] | false;
  out.creditsUsedMinor = doc["credits_used_minor"] | 0;
  out.creditsLimitMinor = doc["credits_limit_minor"] | 0;
  const char *currency = doc["currency"] | "USD";
  out.currency = currency;

  out.valid = true;
  out.error = "";
  return true;
}

} // namespace RelayClient
