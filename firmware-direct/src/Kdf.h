// PBKDF2-HMAC-SHA256 key derivation - turns a human passphrase into the
// AES-256 key that protects the stored cookie. Single-block (32-byte
// output exactly matches HMAC-SHA256's output size, so RFC 8018's
// multi-block U-value concatenation never applies here).
#pragma once

#include <Arduino.h>

namespace Kdf {

void pbkdf2Sha256(const String &passphrase, const uint8_t salt[16], uint32_t iterations, uint8_t out[32]);

} // namespace Kdf
