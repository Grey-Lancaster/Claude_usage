#include "ClaudeUsageClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "Iso8601.h"
#include "RootCA.h"

namespace ClaudeUsageClient {
namespace {

String formatResetsIn(const String &resetsAtIso) {
  time_t target = Iso8601::parseUtc(resetsAtIso);
  time_t now = time(nullptr);
  if (target == 0 || now < 1700000000) return "?";  // clock not NTP-synced yet, or unparseable

  long diff = (long)(target - now);
  if (diff <= 0) return "now";

  long hours = diff / 3600;
  long minutes = (diff % 3600) / 60;
  char buf[16];
  if (hours > 0) {
    snprintf(buf, sizeof(buf), "%ldh %ldm", hours, minutes);
  } else {
    snprintf(buf, sizeof(buf), "%ldm", minutes);
  }
  return String(buf);
}

} // namespace

bool fetch(const String &orgId, const String &cookie, UsageDashboard::Snapshot &out) {
  out.valid = false;

  WiFiClientSecure client;
  client.setCACert(CLAUDE_ROOT_CA);

  HTTPClient http;
  String url = "https://claude.ai/api/organizations/" + orgId + "/usage";
  if (!http.begin(client, url)) {
    out.error = "HTTP begin failed";
    return false;
  }
  http.addHeader("Cookie", cookie);
  http.addHeader("User-Agent", "Claude_usage-CYD/1.0 (+https://github.com/Grey-Lancaster/Claude_usage)");
  http.setTimeout(10000);

  int code = http.GET();
  if (code == 401 || code == 403) {
    http.end();
    out.error = "Cookie expired - reconfigure";
    return false;
  }
  if (code != 200) {
    http.end();
    out.error = "HTTP " + String(code);
    return false;
  }

  // Only pull out the two fields this app needs. The full response also
  // carries a pile of unrelated internal flags (feature-flag-looking keys
  // with codenames) - filtering keeps the parse small and forward-
  // compatible with fields we don't care about changing shape.
  StaticJsonDocument<512> filter;
  JsonArray limitsFilter = filter.createNestedArray("limits");
  JsonObject limitFilter = limitsFilter.createNestedObject();
  limitFilter["kind"] = true;
  limitFilter["percent"] = true;
  limitFilter["resets_at"] = true;
  JsonObject spendFilter = filter.createNestedObject("spend");
  spendFilter["enabled"] = true;
  JsonObject usedFilter = spendFilter.createNestedObject("used");
  usedFilter["amount_minor"] = true;
  JsonObject limitAmtFilter = spendFilter.createNestedObject("limit");
  limitAmtFilter["amount_minor"] = true;
  limitAmtFilter["currency"] = true;

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();

  if (err) {
    out.error = "JSON parse: " + String(err.c_str());
    return false;
  }

  bool sawSession = false, sawWeekly = false;
  for (JsonObject limit : doc["limits"].as<JsonArray>()) {
    const char *kind = limit["kind"] | "";
    if (strcmp(kind, "session") == 0) {
      out.sessionPercent = limit["percent"] | 0;
      out.sessionResetsIn = formatResetsIn(limit["resets_at"] | "");
      sawSession = true;
    } else if (strncmp(kind, "weekly", 6) == 0) {
      out.weeklyPercent = limit["percent"] | 0;
      out.weeklyResetsIn = formatResetsIn(limit["resets_at"] | "");
      sawWeekly = true;
    }
  }

  JsonObject spend = doc["spend"];
  out.creditsEnabled = spend["enabled"] | false;
  out.creditsUsedMinor = spend["used"]["amount_minor"] | 0;
  out.creditsLimitMinor = spend["limit"]["amount_minor"] | 0;
  const char *currency = spend["limit"]["currency"] | "USD";
  out.currency = currency;

  if (!sawSession && !sawWeekly) {
    out.error = "No limits in response";
    return false;
  }

  out.valid = true;
  out.error = "";
  return true;
}

} // namespace ClaudeUsageClient
