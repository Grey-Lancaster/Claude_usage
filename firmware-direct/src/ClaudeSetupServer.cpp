#include "ClaudeSetupServer.h"

#include <ESPmDNS.h>
#include <WebServer.h>

#include "ClaudeConfig.h"

namespace ClaudeSetupServer {
namespace {

WebServer server(80);
std::function<void()> onUnlocked;
bool lastUnlockFailed = false;

const char *PAGE_HEAD =
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
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
        "find the request to <code>/api/organizations/&lt;ORG_ID&gt;/usage</code>.</p>"
        "<form method='POST' action='/provision'>"
        "<label>Org ID</label><input name='orgid' required>"
        "<label>Session cookie (the request's 'cookie' header)</label><textarea name='cookie' required></textarea>"
        "<label>New passphrase (not stored on this device - remember it)</label>"
        "<input type='password' name='pass' required>"
        "<label>Confirm passphrase</label><input type='password' name='pass2' required>"
        "<button type='submit'>Save</button></form>";
  } else if (!ClaudeConfig::isUnlocked()) {
    body = "<h1>Locked</h1><p>Enter your passphrase to unlock.</p>";
    if (lastUnlockFailed) body += "<p class='err'>Wrong passphrase.</p>";
    body +=
        "<form method='POST' action='/unlock'>"
        "<label>Passphrase</label><input type='password' name='pass' required>"
        "<button type='submit'>Unlock</button></form>"
        "<hr><form method='POST' action='/reset' "
        "onsubmit=\"return confirm('Erase the saved account and start over?');\">"
        "<button class='danger' type='submit'>Forgot passphrase - reset</button></form>";
  } else {
    body =
        "<h1>Unlocked</h1><p class='ok'>Dashboard is polling normally.</p>"
        "<form method='POST' action='/reset' "
        "onsubmit=\"return confirm('Erase the saved account and start over?');\">"
        "<button class='danger' type='submit'>Reset Claude account</button></form>";
  }

  sendPage(body);
}

void handleProvision() {
  String orgId = server.arg("orgid");
  String cookie = server.arg("cookie");
  String pass = server.arg("pass");
  String pass2 = server.arg("pass2");

  orgId.trim();
  cookie.trim();

  if (orgId.length() == 0 || cookie.length() == 0 || pass.length() == 0 || pass != pass2) {
    sendPage("<h1>Setup failed</h1><p class='err'>Missing fields or passphrases didn't match.</p><p><a href='/'>Back</a></p>");
    return;
  }

  ClaudeConfig::provision(orgId, cookie, pass);
  lastUnlockFailed = false;
  if (onUnlocked) onUnlocked();
  redirectHome();
}

void handleUnlock() {
  bool ok = ClaudeConfig::unlock(server.arg("pass"));
  lastUnlockFailed = !ok;
  if (ok && onUnlocked) onUnlocked();
  redirectHome();
}

void handleReset() {
  ClaudeConfig::forget();
  lastUnlockFailed = false;
  redirectHome();
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

} // namespace

void begin() {
  MDNS.begin("claudeusage");
  MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/provision", HTTP_POST, handleProvision);
  server.on("/unlock", HTTP_POST, handleUnlock);
  server.on("/reset", HTTP_POST, handleReset);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Setup page: http://claudeusage.local/");
}

void handleClient() { server.handleClient(); }

void setOnUnlocked(std::function<void()> cb) { onUnlocked = cb; }

} // namespace ClaudeSetupServer
