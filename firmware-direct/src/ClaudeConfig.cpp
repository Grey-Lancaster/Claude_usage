#include "ClaudeConfig.h"

#include <Preferences.h>

namespace ClaudeConfig {
namespace {

const char *PREFS_NAMESPACE = "claudecfg";

Preferences prefs;
String cachedOrgId;
String cachedCookie;

enum class PromptState { Idle, AwaitingOrgId, AwaitingCookie };
PromptState promptState = PromptState::Idle;
String pendingOrgId;
String lineBuf;

void save(const String &orgIdVal, const String &cookieVal) {
  cachedOrgId = orgIdVal;
  cachedCookie = cookieVal;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString("orgid", orgIdVal);
  prefs.putString("cookie", cookieVal);
  prefs.end();
}

} // namespace

void begin() {
  prefs.begin(PREFS_NAMESPACE, true);
  cachedOrgId = prefs.getString("orgid", "");
  cachedCookie = prefs.getString("cookie", "");
  prefs.end();
}

bool isConfigured() { return cachedOrgId.length() > 0 && cachedCookie.length() > 0; }
String orgId() { return cachedOrgId; }
String cookie() { return cachedCookie; }

void clear() {
  save("", "");
}

void startSetup() {
  promptState = PromptState::AwaitingOrgId;
  pendingOrgId = "";
  lineBuf = "";
  Serial.println();
  Serial.println("=== Claude account setup ===");
  Serial.println("1) In a browser logged into claude.ai, open DevTools > Network,");
  Serial.println("   reload Settings > Usage, and find the request to:");
  Serial.println("     /api/organizations/<ORG_ID>/usage");
  Serial.println("2) Paste the ORG_ID (the UUID in that URL) below, then Enter.");
  Serial.print("Org ID: ");
}

void pollSerial() {
  if (promptState == PromptState::Idle) {
    // Not armed by startSetup(), but still let "setup" typed at any time
    // (e.g. after a misconfigured cookie) re-arm it without a reboot.
    if (Serial.available()) {
      String cmd = Serial.readStringUntil('\n');
      cmd.trim();
      if (cmd.equalsIgnoreCase("setup")) startSetup();
    }
    return;
  }

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      lineBuf += c;
      continue;
    }

    String line = lineBuf;
    line.trim();
    lineBuf = "";

    if (promptState == PromptState::AwaitingOrgId) {
      if (line.length() == 0) {
        Serial.print("Org ID: ");
        continue;
      }
      pendingOrgId = line;
      promptState = PromptState::AwaitingCookie;
      Serial.println(line);
      Serial.println("3) In the same request, copy the full 'cookie' request header value.");
      Serial.print("Cookie: ");
    } else if (promptState == PromptState::AwaitingCookie) {
      if (line.length() == 0) {
        Serial.print("Cookie: ");
        continue;
      }
      save(pendingOrgId, line);
      promptState = PromptState::Idle;
      Serial.println("[hidden]");
      Serial.println("Saved. Rebooting...");
      delay(300);
      ESP.restart();
    }
  }
}

} // namespace ClaudeConfig
