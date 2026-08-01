#include "Kdf.h"

#include <cstring>
#include <mbedtls/md.h>

namespace Kdf {
namespace {

// One-shot mbedtls_md_hmac() re-derives the HMAC key schedule (processes
// the key into ipad/opad state) on every call - fine for a single HMAC,
// disastrous 100k times in a loop (measured: tens of seconds on this
// chip). mbedtls_md_hmac_starts() does that key processing once;
// mbedtls_md_hmac_reset() cheaply rewinds to right after it for the next
// iteration, without repeating it - this is the pattern mbedtls's own
// PBKDF2 implementation uses internally.
struct HmacCtx {
  mbedtls_md_context_t md;
  HmacCtx(const String &key) {
    mbedtls_md_init(&md);
    mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&md, (const uint8_t *)key.c_str(), key.length());
  }
  ~HmacCtx() { mbedtls_md_free(&md); }
  void compute(const uint8_t *data, size_t len, uint8_t out[32]) {
    mbedtls_md_hmac_reset(&md);
    mbedtls_md_hmac_update(&md, data, len);
    mbedtls_md_hmac_finish(&md, out);
  }
};

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

  HmacCtx hmac(passphrase);

  uint8_t u[32];
  hmac.compute(block, sizeof(block), u);
  memcpy(out, u, 32);

  for (uint32_t i = 1; i < iterations; i++) {
    hmac.compute(u, 32, u);
    for (int b = 0; b < 32; b++) out[b] ^= u[b];
  }
}

} // namespace Kdf
