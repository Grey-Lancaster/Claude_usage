#include "ClaudeConfig.h"

#include <Preferences.h>
#include <cstdlib>
#include <cstring>
#include <esp_system.h>
#include <mbedtls/gcm.h>

#include "Kdf.h"

namespace ClaudeConfig {
namespace {

const char *PREFS_NAMESPACE = "claudecfg";
// 20k HMAC-SHA256 rounds via a reused context (see Kdf.cpp) - a
// deliberate, human-noticeable-but-not-annoying cost on unlock, several
// orders of magnitude more expensive per guess than hashing the
// passphrase once. Originally 100k rounds with a one-shot HMAC call per
// round (re-deriving the key schedule every time instead of reusing it),
// which took long enough to make the browser give up waiting for the
// setup page's response before the device even finished. Actual timing
// for the current settings prints to Serial on every provision/unlock.
const uint32_t PBKDF2_ITERATIONS = 20000;
const size_t SALT_LEN = 16;
const size_t NONCE_LEN = 12;
const size_t TAG_LEN = 16;

Preferences prefs;

String cachedOrgId;
String cachedTimezone;
bool provisioned = false;
bool unlocked = false;
String plaintextCookie;  // RAM only - never written to Preferences

void randomBytes(uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)esp_random();
}

bool gcmDecrypt(const uint8_t key[32], const uint8_t *nonce, const uint8_t *tag,
                 const uint8_t *ciphertext, size_t len, String &outPlain) {
  if (len == 0) {
    outPlain = "";
    return true;
  }
  uint8_t *plain = (uint8_t *)malloc(len);
  if (!plain) return false;

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
  int rc = mbedtls_gcm_auth_decrypt(&ctx, len, nonce, NONCE_LEN, nullptr, 0, tag, TAG_LEN, ciphertext, plain);
  mbedtls_gcm_free(&ctx);

  if (rc != 0) {
    free(plain);
    return false;  // wrong passphrase, or corrupted/tampered data
  }
  outPlain = String((const char *)plain, len);
  free(plain);
  return true;
}

} // namespace

void begin() {
  prefs.begin(PREFS_NAMESPACE, true);
  cachedOrgId = prefs.getString("orgid", "");
  cachedTimezone = prefs.getString("tz", "UTC0");  // POSIX TZ rule, not IANA name - see ClaudeSetupServer.cpp
  provisioned = cachedOrgId.length() > 0 && prefs.isKey("ciphertext");
  prefs.end();
}

bool isProvisioned() { return provisioned; }
bool isUnlocked() { return unlocked; }
String orgId() { return cachedOrgId; }
String cookie() { return plaintextCookie; }
String timezone() { return cachedTimezone; }

void setTimezone(const String &timezoneVal) {
  cachedTimezone = timezoneVal;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString("tz", timezoneVal);
  prefs.end();
}

void provision(const String &orgIdVal, const String &cookieVal, const String &passphrase, const String &timezoneVal) {
  uint8_t salt[SALT_LEN];
  uint8_t nonce[NONCE_LEN];
  randomBytes(salt, SALT_LEN);
  randomBytes(nonce, NONCE_LEN);

  unsigned long kdfStart = millis();
  uint8_t key[32];
  Kdf::pbkdf2Sha256(passphrase, salt, PBKDF2_ITERATIONS, key);
  Serial.printf("[claudeconfig] provision: PBKDF2 (%lu iterations) took %lu ms\n", (unsigned long)PBKDF2_ITERATIONS,
                millis() - kdfStart);

  size_t len = cookieVal.length();
  uint8_t *cipher = (uint8_t *)malloc(len > 0 ? len : 1);
  uint8_t tag[TAG_LEN];

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
  mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, len, nonce, NONCE_LEN, nullptr, 0,
                             (const uint8_t *)cookieVal.c_str(), cipher, TAG_LEN, tag);
  mbedtls_gcm_free(&ctx);

  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString("orgid", orgIdVal);
  prefs.putString("tz", timezoneVal);
  prefs.putBytes("salt", salt, SALT_LEN);
  prefs.putBytes("nonce", nonce, NONCE_LEN);
  prefs.putBytes("tag", tag, TAG_LEN);
  prefs.putBytes("ciphertext", cipher, len);
  prefs.end();

  free(cipher);

  cachedOrgId = orgIdVal;
  cachedTimezone = timezoneVal;
  plaintextCookie = cookieVal;
  provisioned = true;
  unlocked = true;
}

bool unlock(const String &passphrase) {
  if (!provisioned) return false;

  prefs.begin(PREFS_NAMESPACE, true);
  bool sizesOk = prefs.getBytesLength("salt") == SALT_LEN &&
                 prefs.getBytesLength("nonce") == NONCE_LEN &&
                 prefs.getBytesLength("tag") == TAG_LEN;
  if (!sizesOk) {
    prefs.end();
    return false;
  }

  uint8_t salt[SALT_LEN], nonce[NONCE_LEN], tag[TAG_LEN];
  prefs.getBytes("salt", salt, SALT_LEN);
  prefs.getBytes("nonce", nonce, NONCE_LEN);
  prefs.getBytes("tag", tag, TAG_LEN);

  size_t cipherLen = prefs.getBytesLength("ciphertext");
  uint8_t *cipher = (uint8_t *)malloc(cipherLen > 0 ? cipherLen : 1);
  prefs.getBytes("ciphertext", cipher, cipherLen);
  prefs.end();

  unsigned long kdfStart = millis();
  uint8_t key[32];
  Kdf::pbkdf2Sha256(passphrase, salt, PBKDF2_ITERATIONS, key);
  Serial.printf("[claudeconfig] unlock: PBKDF2 (%lu iterations) took %lu ms\n", (unsigned long)PBKDF2_ITERATIONS,
                millis() - kdfStart);

  String plain;
  bool ok = gcmDecrypt(key, nonce, tag, cipher, cipherLen, plain);
  free(cipher);
  if (!ok) return false;

  plaintextCookie = plain;
  unlocked = true;
  return true;
}

void forget() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.clear();
  prefs.end();
  cachedOrgId = "";
  cachedTimezone = "UTC0";
  plaintextCookie = "";
  provisioned = false;
  unlocked = false;
}

} // namespace ClaudeConfig
