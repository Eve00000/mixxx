#include "util/blowfish.h"

#include <QCryptographicHash>

#include <cstring>

#include "util/blowfishtables.h"

namespace mixxx {
namespace crypto {

namespace {

// Blowfish F-function.
inline uint32_t f(uint32_t x, const uint32_t s[4][256]) {
    return ((s[0][(x >> 24) & 0xff] + s[1][(x >> 16) & 0xff]) ^
                   s[2][(x >> 8) & 0xff]) +
            s[3][x & 0xff];
}

inline uint32_t readBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
            (static_cast<uint32_t>(p[1]) << 16) |
            (static_cast<uint32_t>(p[2]) << 8) |
            static_cast<uint32_t>(p[3]);
}

inline void writeBe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

} // namespace

Blowfish::Blowfish(const uint8_t* key, int keyLength) {
    // Initial P-array and S-boxes.
    std::memcpy(mP, kBlowfishP, sizeof(mP));
    std::memcpy(mS, kBlowfishS, sizeof(mS));

    // XOR the P-array with the key, wrapping the key if it is shorter.
    for (int i = 0; i < 18; ++i) {
        uint32_t data = 0;
        for (int k = 0; k < 4; ++k) {
            const uint8_t byte =
                    (keyLength > 0) ? key[(i * 4 + k) % keyLength] : 0;
            data = (data << 8) | byte;
        }
        mP[i] ^= data;
    }

    // Key-dependent expansion, encrypting an all-zero block repeatedly.
    uint32_t l = 0;
    uint32_t r = 0;
    for (int i = 0; i < 18; i += 2) {
        encryptBlock(l, r);
        mP[i] = l;
        mP[i + 1] = r;
    }
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 256; j += 2) {
            encryptBlock(l, r);
            mS[i][j] = l;
            mS[i][j + 1] = r;
        }
    }
}

void Blowfish::encryptBlock(uint32_t& left, uint32_t& right) const {
    uint32_t l = left;
    uint32_t r = right;
    for (int i = 0; i < 16; i += 2) {
        l ^= mP[i];
        r ^= f(l, mS);
        r ^= mP[i + 1];
        l ^= f(r, mS);
    }
    l ^= mP[16];
    r ^= mP[17];
    left = r;
    right = l;
}

void Blowfish::decryptBlock(uint32_t& left, uint32_t& right) const {
    uint32_t l = left;
    uint32_t r = right;
    for (int i = 17; i > 1; i -= 2) {
        l ^= mP[i];
        r ^= f(l, mS);
        r ^= mP[i - 1];
        l ^= f(r, mS);
    }
    l ^= mP[1];
    r ^= mP[0];
    left = r;
    right = l;
}

void Blowfish::decryptCbc(const uint8_t* input,
        uint8_t* output,
        int length,
        const uint8_t iv[8]) const {
    uint32_t prev0 = readBe32(iv);
    uint32_t prev1 = readBe32(iv + 4);

    for (int offset = 0; offset + 8 <= length; offset += 8) {
        const uint32_t c0 = readBe32(input + offset);
        const uint32_t c1 = readBe32(input + offset + 4);

        uint32_t l = c0;
        uint32_t r = c1;
        decryptBlock(l, r);
        const uint32_t p0 = l ^ prev0;
        const uint32_t p1 = r ^ prev1;

        writeBe32(output + offset, p0);
        writeBe32(output + offset + 4, p1);

        prev0 = c0;
        prev1 = c1;
    }
}

void generateDeezerBlowfishKey(qint64 trackId, uint8_t keyOut[16]) {
    static const char kSecret[] = "g4el58wc0zvf9na1";

    // idMd5 = md5(ascii(trackId)); key[i] = md5[i] ^ md5[i+16] ^ secret[i]
    const QByteArray hash = QCryptographicHash::hash(
            QByteArray::number(trackId), QCryptographicHash::Md5);
    for (int i = 0; i < 16; ++i) {
        keyOut[i] = static_cast<uint8_t>(hash.at(i)) ^
                static_cast<uint8_t>(hash.at(16 + i)) ^
                static_cast<uint8_t>(kSecret[i]);
    }
}

} // namespace crypto
} // namespace mixxx
