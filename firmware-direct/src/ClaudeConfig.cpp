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
// ~100k HMAC-SHA256 rounds - a deliberate, human-noticeable-but-not-
// annoying cost on unlock (well under a second on this chip's hardware
// SHA accelerator), several orders of magnitude more expensive per guess
// than hashing the passphrase once.
const uint32_t PBKDF2_ITERATIONS = 100000;
const size_t SALT_LEN = 16;
const size_t NONCE_LEN = 12;
const size_t TAG_LEN = 16;

Preferences prefs;

String cachedOrgId;
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
  provisioned = cachedOrgId.length() > 0 && prefs.isKey("ciphertext");
  prefs.end();
}

bool isProvisioned() { return provisioned; }
bool isUnlocked() { return unlocked; }
String orgId() { return cachedOrgId; }
String cookie() { return plaintextCookie; }

void provision(const String &orgIdVal, const String &cookieVal, const String &passphrase) {
  uint8_t salt[SALT_LEN];
  uint8_t nonce[NONCE_LEN];
  randomBytes(salt, SALT_LEN);
  randomBytes(nonce, NONCE_LEN);

  uint8_t key[32];
  Kdf::pbkdf2Sha256(passphrase, salt, PBKDF2_ITERATIONS, key);

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
  prefs.putBytes("salt", salt, SALT_LEN);
  prefs.putBytes("nonce", nonce, NONCE_LEN);
  prefs.putBytes("tag", tag, TAG_LEN);
  prefs.putBytes("ciphertext", cipher, len);
  prefs.end();

  free(cipher);

  cachedOrgId = orgIdVal;
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

  uint8_t key[32];
  Kdf::pbkdf2Sha256(passphrase, salt, PBKDF2_ITERATIONS, key);

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
  plaintextCookie = "";
  provisioned = false;
  unlocked = false;
}

} // namespace ClaudeConfig
