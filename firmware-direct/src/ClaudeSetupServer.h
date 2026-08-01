// Tiny on-device web UI at http://claudeusage.local/ for provisioning
// and unlocking the encrypted cookie (see ClaudeConfig.h). A browser
// text field handles a 100+ char cookie far better than either a serial
// terminal or the on-screen keyboard.
//
// Plain HTTP, no auth beyond the passphrase itself - anyone on your LAN
// can load the page, but without the right passphrase all they can do is
// guess (rate-limited only by PBKDF2's ~100k-round cost per attempt) or
// wipe the stored config via Reset, never read the cookie. The one-time
// act of typing the cookie/passphrase into this page does cross your LAN
// as plaintext HTTP, same caveat as any admin page on a device this
// size - fine on a trusted home network, not something to expose beyond
// it.
#pragma once

#include <functional>

namespace ClaudeSetupServer {

// Starts the mDNS responder ("claudeusage.local") and the HTTP server.
// Call once after Wi-Fi connects.
void begin();

// Call every loop() iteration.
void handleClient();

// Fires after a successful provision or unlock (i.e. whenever the cookie
// just became available in RAM) - the host app hooks this to trigger an
// immediate poll instead of waiting for the next scheduled one.
void setOnUnlocked(std::function<void()> cb);

} // namespace ClaudeSetupServer
