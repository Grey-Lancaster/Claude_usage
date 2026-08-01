#include "ClaudeUsageClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ezTime.h>
#include <time.h>

#include "Iso8601.h"
#include "RootCA.h"

// dateTime()/minute() bounce through to whichever Timezone main.cpp last
// called setDefault() on - this file never needs its own Timezone
// reference, just namespace ezt's free functions.
using namespace ezt;

namespace ClaudeUsageClient {
namespace {

// Session (5hr) resets soon enough that a countdown is more useful than a
// clock time.
String formatResetsIn(const String &resetsAtIso) {
  time_t target = Iso8601::parseUtc(resetsAtIso);
  // UTC.now() (ezTime's dedicated always-UTC instance), not time(nullptr)
  // - ezTime keeps its own internal clock and never calls settimeofday(),
  // so the plain system clock stays stuck near the epoch forever now that
  // main.cpp uses waitForSync() instead of configTime(). Also not
  // myTZ.now(), which returns zone-*adjusted* local time - target (from
  // Iso8601::parseUtc) is plain UTC, so both sides of the diff need to be.
  time_t now = UTC.now();
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

// Weekly resets days out, where a countdown ("123h 6m") is much less
// readable than a day + clock time. Formatted in whatever timezone was
// picked on the setup page (see main.cpp's myTZ) - dropping the minutes
// when the reset lands exactly on the hour, matching "Thursday 4PM"
// rather than always forcing "Thursday 4:00PM".
String formatResetsAt(const String &resetsAtIso) {
  time_t target = Iso8601::parseUtc(resetsAtIso);
  // UTC.now() (ezTime's dedicated always-UTC instance), not time(nullptr)
  // - ezTime keeps its own internal clock and never calls settimeofday(),
  // so the plain system clock stays stuck near the epoch forever now that
  // main.cpp uses waitForSync() instead of configTime(). Also not
  // myTZ.now(), which returns zone-*adjusted* local time - target (from
  // Iso8601::parseUtc) is plain UTC, so both sides of the diff need to be.
  time_t now = UTC.now();
  if (target == 0 || now < 1700000000) return "?";  // clock not NTP-synced yet, or unparseable

  // Must pass UTC_TIME explicitly - dateTime(t, format) bounces to
  // dateTime(t, LOCAL_TIME, format), and LOCAL_TIME mode treats the t you
  // hand it as *already local* (skips the UTC->local conversion, only
  // borrows the zone's name/offset for formatting - see ezTime.cpp
  // Timezone::dateTime). target is plain UTC from Iso8601::parseUtc, so
  // without UTC_TIME here the offset never gets applied and the clock time
  // silently comes out shifted by the zone's UTC offset (e.g. showing
  // 7:59pm when local was really 3:59pm, a 4h EDT-sized gap).
  return dateTime(target, UTC_TIME, minute(target, UTC_TIME) == 0 ? "l gA" : "l g:iA");
}

} // namespace

bool fetch(const String &orgId, const String &cookie, UsageDashboard::Snapshot &out) {
  out.valid = false;

  WiFiClientSecure client;
  client.setCACert(CLAUDE_ROOT_CA);

  HTTPClient http;
  String url = "https://claude.ai/api/organizations/" + orgId + "/usage";
  Serial.printf("[claude] GET %s (cookie len %d)\n", url.c_str(), cookie.length());
  if (!http.begin(client, url)) {
    Serial.println("[claude] http.begin() failed");
    out.error = "HTTP begin failed";
    return false;
  }
  http.addHeader("Cookie", cookie);
  http.addHeader("User-Agent", "Claude_usage-CYD/1.0 (+https://github.com/Grey-Lancaster/Claude_usage)");
  // HTTPClient doesn't auto-decompress - without this, a gzip'd response
  // (common behind a CDN) reads as garbage bytes to the JSON parser.
  http.addHeader("Accept-Encoding", "identity");
  http.setTimeout(10000);

  int code = http.GET();
  Serial.printf("[claude] -> HTTP %d\n", code);
  if (code <= 0) {
    // Negative codes are HTTPClient's own error enum (connect/TLS/timeout
    // failures never reached a server response) - print what it means.
    Serial.printf("[claude] transport error: %s\n", http.errorToString(code).c_str());
  } else if (code != 200) {
    String body = http.getString();
    Serial.printf("[claude] response body (first 300 chars): %s\n", body.substring(0, 300).c_str());
  }
  if (code == 401 || code == 403) {
    http.end();
    out.error = "Cookie expired - reconfigure";
    return false;
  }
  if (code != 200) {
    http.end();
    // A raw "HTTP 400" reads as noise to someone who isn't debugging this
    // over serial - the real code is still logged above for that case. In
    // practice this path fires almost entirely from a mistyped/truncated
    // org ID or cookie (a malformed org ID makes claude.ai's API reject
    // the URL outright), so point at the actual fix instead of the code.
    out.error = "Something's wrong - check your org ID/cookie and try again";
    return false;
  }

  // getString() (not the stream) so the raw bytes are still around to
  // print if parsing fails - a streamed parse loses them on error, which
  // cost a whole extra flash-and-test cycle chasing a gzip-encoding issue
  // blind.
  String body = http.getString();
  http.end();

  // Parses the full response rather than filtering it down to just
  // limits/spend - a StaticJsonDocument filter here previously undersized
  // itself (ArduinoJson's per-slot overhead for nested filter
  // objects/arrays is easy to miscalculate by hand) and silently filtered
  // out everything, not just the fields we didn't want. The full response
  // is ~2KB; 4096 bytes leaves comfortable headroom without needing to
  // get that arithmetic exactly right.
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    Serial.printf("[claude] JSON parse error: %s (body length %d)\n", err.c_str(), body.length());
    Serial.print("[claude] first 16 bytes (hex):");
    for (int i = 0; i < 16 && i < (int)body.length(); i++) Serial.printf(" %02X", (uint8_t)body[i]);
    Serial.println();
    Serial.printf("[claude] first 200 chars (raw): %s\n", body.substring(0, 200).c_str());
    out.error = "JSON parse: " + String(err.c_str());
    return false;
  }

  Serial.printf("[claude] parsed JSON body (%d bytes)\n", body.length());

  bool sawSession = false, sawWeekly = false;
  for (JsonObject limit : doc["limits"].as<JsonArray>()) {
    const char *kind = limit["kind"] | "";
    if (strcmp(kind, "session") == 0) {
      out.sessionPercent = limit["percent"] | 0;
      out.sessionResetsIn = "Resets in " + formatResetsIn(limit["resets_at"] | "");
      sawSession = true;
    } else if (strncmp(kind, "weekly", 6) == 0) {
      out.weeklyPercent = limit["percent"] | 0;
      out.weeklyResetsIn = "Resets " + formatResetsAt(limit["resets_at"] | "");
      sawWeekly = true;
    }
  }

  JsonObject spend = doc["spend"];
  out.creditsEnabled = spend["enabled"] | false;
  out.creditsUsedMinor = spend["used"]["amount_minor"] | 0;
  out.creditsLimitMinor = spend["limit"]["amount_minor"] | 0;
  const char *currency = spend["limit"]["currency"] | "USD";
  out.currency = currency;

  Serial.printf("[claude] parsed: session=%d%% (%s) weekly=%d%% (%s) credits enabled=%d used=%ld/%ld %s\n",
                out.sessionPercent, out.sessionResetsIn.c_str(), out.weeklyPercent, out.weeklyResetsIn.c_str(),
                out.creditsEnabled, out.creditsUsedMinor, out.creditsLimitMinor, out.currency.c_str());

  if (!sawSession && !sawWeekly) {
    Serial.println("[claude] no 'session' or 'weekly*' entries found in limits[] - schema may have changed");
    out.error = "No limits in response";
    return false;
  }

  out.valid = true;
  out.error = "";
  return true;
}

} // namespace ClaudeUsageClient
