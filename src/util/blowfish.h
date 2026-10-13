#pragma once

#include <QtGlobal>

#include <cstdint>

namespace mixxx {
namespace crypto {

/// Minimal Blowfish implementation, enough to decrypt Deezer's
/// "BF_CBC_STRIPE" media streams.
///
/// The algorithm is the public Blowfish cipher (Bruce Schneier, 1993). The
/// P-array and S-boxes are the standard digits-of-pi constants defined in the
/// specification. This class is deliberately small and dependency-free because
/// Mixxx does not link OpenSSL's libcrypto (Qt uses the system TLS backend).
///
/// Thread-safety: `decryptCbc` only reads `mP`/`mS`, so once initialized a key
/// may be shared across threads. Initialization (the constructor) is not
/// thread-safe with respect to concurrent use.
class Blowfish {
  public:
    /// Expands the key into the P-array and S-boxes.
    Blowfish(const uint8_t* key, int keyLength);

    Blowfish(const Blowfish&) = delete;
    Blowfish& operator=(const Blowfish&) = delete;

    /// Decrypts `length` bytes from `input` into `output` using CBC mode with
    /// the given 8-byte IV. `length` must be a multiple of 8. `input` and
    /// `output` may alias.
    void decryptCbc(
            const uint8_t* input,
            uint8_t* output,
            int length,
            const uint8_t iv[8]) const;

  private:
    void encryptBlock(uint32_t& left, uint32_t& right) const;
    void decryptBlock(uint32_t& left, uint32_t& right) const;

    uint32_t mP[18];
    uint32_t mS[4][256];
};

/// Generates the 16-byte Blowfish key Deezer uses for a track, derived from the
/// track id and the constant "secret".
void generateDeezerBlowfishKey(qint64 trackId, uint8_t keyOut[16]);

} // namespace crypto
} // namespace mixxx
