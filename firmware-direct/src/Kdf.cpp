#include "Kdf.h"

#include <cstring>
#include <mbedtls/md.h>

namespace Kdf {
namespace {

void hmacSha256(const String &key, const uint8_t *data, size_t dataLen, uint8_t out[32]) {
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, (const uint8_t *)key.c_str(), key.length(), data, dataLen, out);
}

} // namespace

void pbkdf2Sha256(const String &passphrase, const uint8_t salt[16], uint32_t iterations, uint8_t out[32]) {
  // RFC 8018 5.2, single block (i=1): U1 = HMAC(P, S || INT_32_BE(1)),
  // T1 = U1 ^ U2 ^ ... ^ Uc.
  uint8_t block[16 + 4];
  memcpy(block, salt, 16);
  block[16] = 0;
  block[17] = 0;
  block[18] = 0;
  block[19] = 1;

  uint8_t u[32];
  hmacSha256(passphrase, block, sizeof(block), u);
  memcpy(out, u, 32);

  for (uint32_t i = 1; i < iterations; i++) {
    hmacSha256(passphrase, u, 32, u);
    for (int b = 0; b < 32; b++) out[b] ^= u[b];
  }
}

} // namespace Kdf
