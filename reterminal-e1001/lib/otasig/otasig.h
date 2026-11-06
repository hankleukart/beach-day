// Release signatures: the board installs firmware only if it was signed with
// the release key (tools/release.sh), whatever the download path or manifest
// says. ECDSA P-256 over the SHA-256 of the whole .bin, sent in the manifest
// as base64 DER - exactly what `openssl dgst -sha256 -sign` writes.
//
// Plain mbedTLS, no Arduino, so the same file is tested on a Mac against the
// board's mbedTLS version.
#pragma once
#include <cstddef>
#include <cstdint>
#include <mbedtls/sha256.h>

namespace otasig {

// The firmware streams through this as it is written to flash.
class Hasher {
 public:
  Hasher();
  ~Hasher();
  Hasher(const Hasher&) = delete;
  Hasher& operator=(const Hasher&) = delete;
  void update(const uint8_t* data, size_t len);
  void finish(uint8_t out[32]);
 private:
  mbedtls_sha256_context ctx_;
};

// True only if sigBase64 is a valid signature of `hash` by the key in
// publicKeyPem. On false, err says why (never the key or signature itself).
bool verify(const uint8_t hash[32], const char* sigBase64, const char* publicKeyPem,
            char* err, size_t errLen);

}  // namespace otasig
