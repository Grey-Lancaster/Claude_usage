// Encrypted-at-rest session cookie storage. The cookie is protected with
// AES-256-GCM under a key derived (PBKDF2-HMAC-SHA256, see Kdf.h) from a
// passphrase you choose at setup time via http://claudeusage.local/ (see
// ClaudeSetupServer.h). That passphrase is never written to flash - only
// held in RAM after a successful unlock. A flash dump therefore only
// yields ciphertext; recovering the real cookie needs the passphrase
// too.
//
// Consequence: the device starts locked after every reboot and needs
// the passphrase re-entered before it can poll claude.ai again. That's
// the point, not a bug - see the README.
#pragma once

#include <Arduino.h>

namespace ClaudeConfig {

void begin();

// True once an encrypted cookie has been stored, regardless of whether
// it's unlocked yet this boot.
bool isProvisioned();

// True once unlock() (or a fresh provision()) has decrypted the cookie
// into RAM this boot.
bool isUnlocked();

String orgId();      // plaintext - not sensitive on its own
String cookie();     // only valid when isUnlocked()
String timezone();   // IANA location (e.g. "America/New_York"), for display formatting only - not sensitive

// First-time setup, or a full replacement: encrypts `cookieVal` under a
// key derived from `passphrase`, persists org id + timezone + ciphertext,
// and immediately unlocks in RAM - no separate unlock step needed right
// after provisioning.
void provision(const String &orgIdVal, const String &cookieVal, const String &passphrase, const String &timezoneVal);

// Changeable independently of the cookie/passphrase - no re-encryption
// involved, it's not a secret.
void setTimezone(const String &timezoneVal);

// Attempts to decrypt the stored cookie with `passphrase`. Returns false
// (device stays locked) on a wrong passphrase or missing data - AES-GCM's
// authentication tag makes a wrong guess fail cleanly instead of
// silently unlocking garbage.
bool unlock(const String &passphrase);

// Wipes everything: org id, ciphertext, and the in-RAM plaintext.
void forget();

} // namespace ClaudeConfig
