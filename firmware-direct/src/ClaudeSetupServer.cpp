#include "ClaudeSetupServer.h"

#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>

#include "ClaudeConfig.h"
#include "OtaPassword.h"
#include "favicon_ico.h"

namespace ClaudeSetupServer {
namespace {

WebServer server(80);
std::function<void()> onUnlocked;
std::function<void(const String &)> onTimezoneChanged;
std::function<void(WebServer &)> onScreenshot;
bool lastUnlockFailed = false;

// See setLastFetchStatus() in the header - everFetched stays false until
// the first attempt ever completes, so a freshly-unlocked device shows
// "waiting for first update" rather than falsely claiming success or
// failure before any fetch has actually run.
bool everFetched = false;
bool lastFetchOk = false;
String lastFetchDetail;

// POSIX TZ rule strings (not IANA location names) so the device can apply
// the right offset + DST rule fully offline via Timezone::setPosix().
// ezTime's own Timezone::setLocation() looks appealingly simpler (pass
// "America/New_York", done) but it resolves that name to a POSIX rule via
// a UDP round-trip to ezTime's own remote lookup server - which measured
// as unreliable/silently-blocked on this network, leaving the timezone
// stuck at UTC with no visible error. POSIX strings sourced from
// https://github.com/nayarsystems/posix_tz_db (a maintained IANA->POSIX
// mapping) - same curated zone list as TouchWifiProvisioner's
// CYD_RollingClock example's SettingsPanel, just a different value format
// per zone for this offline-lookup reason.
struct TzOption {
  const char *label;
  const char *posix;
};
const TzOption TZ_OPTIONS[] = {
    {"Hawaii (US)", "HST10"},
    {"Alaska (US)", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Pacific (US)", "PST8PDT,M3.2.0,M11.1.0"},
    {"Mountain (US)", "MST7MDT,M3.2.0,M11.1.0"},
    {"Central (US)", "CST6CDT,M3.2.0,M11.1.0"},
    {"Eastern (US)", "EST5EDT,M3.2.0,M11.1.0"},
    {"Atlantic Canada", "AST4ADT,M3.2.0,M11.1.0"},
    {"Mexico City", "CST6"},
    {"Bogota", "<-05>5"},
    {"Sao Paulo (Brazil)", "<-03>3"},
    {"Buenos Aires", "<-03>3"},
    {"UTC", "UTC0"},
    {"London (UK)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Western Europe", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Eastern Europe", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Moscow", "MSK-3"},
    {"Istanbul", "<+03>-3"},
    {"Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Johannesburg", "SAST-2"},
    {"Lagos", "WAT-1"},
    {"Dubai", "<+04>-4"},
    {"Tel Aviv", "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"Karachi", "PKT-5"},
    {"India", "IST-5:30"},
    {"Dhaka", "<+06>-6"},
    {"Bangkok", "<+07>-7"},
    {"Singapore", "<+08>-8"},
    {"Hong Kong", "HKT-8"},
    {"China", "CST-8"},
    {"Japan", "JST-9"},
    {"Korea", "KST-9"},
    {"Sydney (Australia)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Perth (Australia)", "AWST-8"},
    {"Auckland (NZ)", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};
constexpr int TZ_OPTION_COUNT = sizeof(TZ_OPTIONS) / sizeof(TZ_OPTIONS[0]);

String buildTzOptionsHtml(const String &current) {
  String html;
  for (int i = 0; i < TZ_OPTION_COUNT; i++) {
    html += "<option value='";
    html += TZ_OPTIONS[i].posix;
    html += "'";
    if (current == TZ_OPTIONS[i].posix) html += " selected";
    html += ">";
    html += TZ_OPTIONS[i].label;
    html += "</option>";
  }
  return html;
}

// Plain-HTML forms here do a real page-navigation POST (no fetch/AJAX), so
// during PBKDF2's ~2-3s unlock/provision delay the only built-in feedback
// is the browser's own tab spinner - easy to miss, especially on a phone,
// and it invites repeat taps on the button. This buys a visible "it's
// working" state for free without a JS framework: adjacent string literals
// concatenate at compile time, so BUSY_BUTTON_JS("Unlocking...") splices
// into one literal wherever it's used inside a form's onsubmit attribute.
#define BUSY_BUTTON_JS(label) \
  "this.querySelector('button').disabled=true;this.querySelector('button').textContent='" label "';"

const char *PAGE_HEAD =
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<link rel='icon' href='/favicon.ico'>"
    "<title>Claude Usage</title><style>"
    "body{background:#111;color:#eee;font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}"
    "h1{font-size:1.3em}label{display:block;margin-top:1em;color:#aaa;font-size:.9em}"
    "input,textarea{width:100%;box-sizing:border-box;padding:.5em;margin-top:.3em;background:#222;color:#eee;"
    "border:1px solid #444;border-radius:6px;font-size:1em}"
    "textarea{height:5em}button{margin-top:1.2em;padding:.6em 1.2em;background:#2f6fed;color:#fff;border:0;"
    "border-radius:6px;font-size:1em}"
    ".danger{background:#3a1a1a;color:#f88}.err{color:#f88}.ok{color:#8f8}"
    "</style></head><body>";
const char *PAGE_TAIL = "</body></html>";

void sendPage(const String &body) {
  server.send(200, "text/html", String(PAGE_HEAD) + body + PAGE_TAIL);
}

void redirectHome() {
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleRoot() {
  String body;

  if (!ClaudeConfig::isProvisioned()) {
    body =
        "<h1>Set up Claude account</h1>"
        "<p>Find these in a browser logged into claude.ai: DevTools &gt; Network, reload Settings &gt; Usage, "
        "find the request to <code>/api/organizations/&lt;ORG_ID&gt;/usage</code>. "
        "<a href='https://github.com/Grey-Lancaster/Claude_usage/blob/main/docs/cookie-guide.md' "
        "style='color:#7ab0ff'>Illustrated guide</a>.</p>"
        "<form method='POST' action='/provision' onsubmit=\"" BUSY_BUTTON_JS("Saving...") "\">"
        "<label>Org ID</label><input name='orgid' required>"
        "<label>Session cookie (the request's 'cookie' header)</label><textarea name='cookie' required></textarea>"
        "<label>Timezone (for displaying reset times)</label><select name='tz'>" +
        buildTzOptionsHtml(ClaudeConfig::timezone()) +
        "</select>"
        "<label>New passphrase (not stored on this device - remember it)</label>"
        "<input type='password' name='pass' required>"
        "<label>Confirm passphrase</label><input type='password' name='pass2' required>"
        "<button type='submit'>Save</button></form>";
  } else if (!ClaudeConfig::isUnlocked()) {
    body = "<h1>Locked</h1><p>Enter your passphrase to unlock.</p>";
    if (lastUnlockFailed) body += "<p class='err'>Wrong passphrase.</p>";
    body +=
        "<form method='POST' action='/unlock' onsubmit=\"" BUSY_BUTTON_JS("Unlocking...") "\">"
        "<label>Passphrase</label><input type='password' name='pass' required>"
        "<button type='submit'>Unlock</button></form>"
        "<hr><form method='POST' action='/reset' "
        "onsubmit=\"return confirm('Erase the saved account and start over?');\">"
        "<button class='danger' type='submit'>Forgot passphrase - reset</button></form>";
  } else {
    body = "<h1>Unlocked</h1>";
    bool settled = everFetched && lastFetchOk;
    if (!everFetched) {
      body += "<p>Waiting for the first update...</p>";
    } else if (lastFetchOk) {
      body += "<p class='ok'>Dashboard is polling normally.</p>";
    } else {
      body += "<p class='err'>Last update failed: " + lastFetchDetail + "</p>";
    }
    if (!settled) {
      // Keeps checking back on its own until the first fetch actually
      // succeeds - each reload re-evaluates the condition above, so this
      // naturally stops adding itself once status settles to "polling
      // normally" instead of needing separate stop logic.
      body += "<script>setTimeout(function(){ location.reload(); }, 10000);</script>";
    }
    body +=
        "<form method='POST' action='/reset' "
        "onsubmit=\"return confirm('Erase the saved account and start over?');\">"
        "<button class='danger' type='submit'>Reset Claude account</button></form>";
  }

  if (ClaudeConfig::isProvisioned()) {
    body +=
        "<hr><form method='POST' action='/timezone'>"
        "<label>Timezone</label><select name='tz'>" +
        buildTzOptionsHtml(ClaudeConfig::timezone()) +
        "</select>"
        "<button type='submit'>Save timezone</button></form>";
  }

  // Unconditional (not gated on provisioned/unlocked state) - the
  // capture itself just grabs whatever's currently on screen, which
  // works fine on the setup/locked screens too, not just the dashboard.
  body +=
      "<hr><p><a href='/screenshot.bmp' target='_blank' style='color:#7ab0ff'>"
      "Screenshot the display</a> - grabs exactly what's on the screen right now, "
      "for READMEs or social posts.</p>";
  body += "<p><a href='/update' style='color:#7ab0ff'>Firmware update</a></p>";
  sendPage(body);
}

void handleTimezone() {
  String tz = server.arg("tz");
  if (tz.length() > 0) {
    ClaudeConfig::setTimezone(tz);
    Serial.printf("[setup] /timezone: set to '%s'\n", tz.c_str());
    if (onTimezoneChanged) onTimezoneChanged(tz);
  }
  redirectHome();
}

void handleUpdatePage() {
  if (!server.authenticate("admin", OTA_PASSWORD)) return server.requestAuthentication();
  sendPage(
      "<h1>Firmware update</h1>"
      "<p>Select a firmware.bin built for this board (<code>pio run</code>, "
      "not the merged \"- Bootable\" image - this only replaces the app, not the bootloader/partitions).</p>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='firmware' accept='.bin' required>"
      "<button type='submit'>Upload &amp; flash</button></form>");
}

// Set when Update.begin() itself fails (as opposed to a write/verify
// failure partway through) - the ESP32 Update library has no public way
// to force-reset its internal state short of a reboot, so a stuck
// begin() will keep failing on every retry until the device is power
// cycled. Worth calling out explicitly rather than showing whatever
// (possibly misleading - e.g. "Not Enough Space" from write()ing against
// stale leftover state) error text Update.errorString() happens to hold.
bool updateBeginFailed = false;

// Fires once after the whole upload request completes - the actual bytes
// were already streamed to Update.write() in handleUpdateUpload() below.
void handleUpdateComplete() {
  if (!server.authenticate("admin", OTA_PASSWORD)) return server.requestAuthentication();

  String body;
  if (updateBeginFailed) {
    body = "<h1>Update failed</h1><p class='err'>Couldn't start (Update.begin() failed - likely a stuck state "
           "from an earlier interrupted attempt). Power cycle the device and try again.</p><p><a href='/update'>Back</a></p>";
  } else if (Update.hasError()) {
    body = "<h1>Update failed</h1><p class='err'>" + String(Update.errorString()) + "</p><p><a href='/update'>Back</a></p>";
  } else {
    body = "<h1>Updated</h1><p class='ok'>Rebooting...</p>";
  }
  server.send(200, "text/html", String(PAGE_HEAD) + body + PAGE_TAIL);

  if (!updateBeginFailed && !Update.hasError()) {
    delay(200);  // let the response above actually reach the browser before the reboot drops the connection
    ESP.restart();
  }
}

void handleUpdateUpload() {
  if (!server.authenticate("admin", OTA_PASSWORD)) return;  // request itself gets rejected in handleUpdateComplete

  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[update] starting: %s\n", upload.filename.c_str());
    updateBeginFailed = !Update.begin(UPDATE_SIZE_UNKNOWN);
    if (updateBeginFailed) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!updateBeginFailed && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!updateBeginFailed) {
      bool ok = Update.end(true);
      Serial.printf("[update] finished: %u bytes, %s\n", upload.totalSize, ok ? "OK" : Update.errorString());
    }
  }
}

void handleProvision() {
  String orgId = server.arg("orgid");
  String cookie = server.arg("cookie");
  String tz = server.arg("tz");
  String pass = server.arg("pass");
  String pass2 = server.arg("pass2");

  orgId.trim();
  cookie.trim();
  // The setup page's form always sends a tz (its <select> has a default
  // selection), but cookie-refresh/refresh_cookie.py posts here too for a
  // cookie-only refresh and doesn't send one at all - falling back to a
  // hardcoded "UTC0" here used to silently wipe out whatever timezone was
  // already configured on every refresh. ClaudeConfig::timezone() already
  // returns "UTC0" itself when nothing's been set yet, so this preserves
  // the existing setting on a refresh while still defaulting sanely for a
  // genuinely first-time provision.
  if (tz.length() == 0) tz = ClaudeConfig::timezone();

  Serial.printf("[setup] /provision: orgId='%s' (len %d), cookie len %d, tz='%s', pass len %d, pass2 len %d, match=%d\n",
                 orgId.c_str(), orgId.length(), cookie.length(), tz.c_str(), pass.length(), pass2.length(), pass == pass2);

  if (orgId.length() == 0 || cookie.length() == 0 || pass.length() == 0 || pass != pass2) {
    Serial.println("[setup] /provision rejected - missing field or passphrase mismatch");
    sendPage("<h1>Setup failed</h1><p class='err'>Missing fields or passphrases didn't match.</p><p><a href='/'>Back</a></p>");
    return;
  }

  ClaudeConfig::provision(orgId, cookie, pass, tz);
  Serial.println("[setup] provisioned + unlocked in RAM");
  lastUnlockFailed = false;
  if (onUnlocked) onUnlocked();
  if (onTimezoneChanged) onTimezoneChanged(tz);
  redirectHome();
}

void handleUnlock() {
  String pass = server.arg("pass");
  bool ok = ClaudeConfig::unlock(pass);
  Serial.printf("[setup] /unlock: pass len %d -> %s\n", pass.length(), ok ? "OK" : "FAILED (wrong passphrase or corrupt data)");
  lastUnlockFailed = !ok;
  if (ok && onUnlocked) onUnlocked();
  redirectHome();
}

void handleReset() {
  ClaudeConfig::forget();
  lastUnlockFailed = false;
  redirectHome();
}

void handleFavicon() {
  server.send_P(200, "image/x-icon", (const char *)favicon_ico, favicon_ico_len);
}

void handleScreenshot() {
  if (!onScreenshot) {
    server.send(404, "text/plain", "No screenshot handler registered for this board");
    return;
  }
  onScreenshot(server);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

} // namespace

void begin() {
  MDNS.begin("claudeusage");
  MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/favicon.ico", HTTP_GET, handleFavicon);
  server.on("/screenshot.bmp", HTTP_GET, handleScreenshot);
  server.on("/provision", HTTP_POST, handleProvision);
  server.on("/unlock", HTTP_POST, handleUnlock);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/timezone", HTTP_POST, handleTimezone);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateComplete, handleUpdateUpload);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Setup page: http://claudeusage.local/");
}

void handleClient() { server.handleClient(); }

void setOnUnlocked(std::function<void()> cb) { onUnlocked = cb; }

void setOnTimezoneChanged(std::function<void(const String &)> cb) { onTimezoneChanged = cb; }

void setLastFetchStatus(bool ok, const String &detail) {
  everFetched = true;
  lastFetchOk = ok;
  lastFetchDetail = detail;
}

void setScreenshotHandler(std::function<void(WebServer &)> handler) { onScreenshot = handler; }

} // namespace ClaudeSetupServer
