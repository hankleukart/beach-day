#include "otasig.h"
#include <cstdio>
#include <cstring>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/version.h>

namespace otasig {

// mbedTLS 3 dropped the _ret suffix; the board's core ships 2.28.
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
Hasher::Hasher() { mbedtls_sha256_init(&ctx_); mbedtls_sha256_starts(&ctx_, 0); }
void Hasher::update(const uint8_t* d, size_t n) { mbedtls_sha256_update(&ctx_, d, n); }
void Hasher::finish(uint8_t out[32]) { mbedtls_sha256_finish(&ctx_, out); }
#else
Hasher::Hasher() { mbedtls_sha256_init(&ctx_); mbedtls_sha256_starts_ret(&ctx_, 0); }
void Hasher::update(const uint8_t* d, size_t n) { mbedtls_sha256_update_ret(&ctx_, d, n); }
void Hasher::finish(uint8_t out[32]) { mbedtls_sha256_finish_ret(&ctx_, out); }
#endif
Hasher::~Hasher() { mbedtls_sha256_free(&ctx_); }

bool verify(const uint8_t hash[32], const char* sigBase64, const char* publicKeyPem,
            char* err, size_t errLen) {
  if (!sigBase64 || !sigBase64[0]) { snprintf(err, errLen, "not signed"); return false; }

  // A P-256 DER signature is at most 72 bytes; anything much longer is not one.
  uint8_t sig[80];
  size_t sigLen = 0;
  if (mbedtls_base64_decode(sig, sizeof(sig), &sigLen, (const uint8_t*)sigBase64, strlen(sigBase64)) != 0 ||
      sigLen == 0) {
    snprintf(err, errLen, "signature is not valid base64");
    return false;
  }

  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  // PEM parsing wants the terminating NUL counted in the length.
  int rc = mbedtls_pk_parse_public_key(&pk, (const uint8_t*)publicKeyPem, strlen(publicKeyPem) + 1);
  if (rc != 0) {
    snprintf(err, errLen, "built-in key unreadable (-0x%04x)", -rc);
    mbedtls_pk_free(&pk);
    return false;
  }
  if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECKEY)) {
    snprintf(err, errLen, "built-in key is not an EC key");
    mbedtls_pk_free(&pk);
    return false;
  }
  rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sigLen);
  mbedtls_pk_free(&pk);
  if (rc != 0) {
    snprintf(err, errLen, "bad signature (-0x%04x)", -rc);
    return false;
  }
  return true;
}

}  // namespace otasig
