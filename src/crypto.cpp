// From-scratch AES-256-GCM and its supporting primitives. No external crypto
// library and no SQLite — this lives in imsg_core so it builds and is KAT-tested
// anywhere a C++17 compiler runs. See crypto.hpp for the API contract and
// tests/test_core.cpp for the authoritative known-answer vectors.
//
// The implementations follow the standards literally rather than chasing speed:
// SHA-256 (FIPS 180-4), HMAC (RFC 2104), PBKDF2 (RFC 8018), the AES block
// cipher (FIPS 197), and GCM mode (NIST SP 800-38D). The byte layout of the
// envelope (salt | iv | ciphertext||tag, all base64) is fixed so the inline JS
// in self_decrypting_html can re-derive it with Web Crypto.
#include "imsg/crypto.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>

namespace imsg {
namespace {

// ===========================================================================
// SHA-256 (FIPS 180-4)
// ===========================================================================

inline std::uint32_t rotr32(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

const std::uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

struct Sha256Ctx {
    std::uint32_t h[8];
    std::uint64_t total_len;     // total message length in bytes
    unsigned char block[64];     // partial block buffer
    std::size_t block_len;       // bytes currently in `block`
};

void sha256_init(Sha256Ctx& c) {
    c.h[0] = 0x6a09e667u; c.h[1] = 0xbb67ae85u; c.h[2] = 0x3c6ef372u;
    c.h[3] = 0xa54ff53au; c.h[4] = 0x510e527fu; c.h[5] = 0x9b05688cu;
    c.h[6] = 0x1f83d9abu; c.h[7] = 0x5be0cd19u;
    c.total_len = 0;
    c.block_len = 0;
}

void sha256_compress(std::uint32_t h[8], const unsigned char* p) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) |
               (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
               (static_cast<std::uint32_t>(p[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 =
            rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 =
            rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], cc = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t t1 = hh + S1 + ch + kSha256K[i] + w[i];
        const std::uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        const std::uint32_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += cc; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256_update(Sha256Ctx& c, const unsigned char* data, std::size_t len) {
    c.total_len += len;
    while (len > 0) {
        const std::size_t take = std::min(len, std::size_t(64) - c.block_len);
        std::memcpy(c.block + c.block_len, data, take);
        c.block_len += take;
        data += take;
        len -= take;
        if (c.block_len == 64) {
            sha256_compress(c.h, c.block);
            c.block_len = 0;
        }
    }
}

void sha256_final(Sha256Ctx& c, unsigned char out[32]) {
    const std::uint64_t bit_len = c.total_len * 8;
    // Append 0x80 then zero-pad so the length fits in the final 8 bytes.
    unsigned char pad = 0x80;
    sha256_update(c, &pad, 1);
    pad = 0x00;
    while (c.block_len != 56) sha256_update(c, &pad, 1);
    unsigned char lenbuf[8];
    for (int i = 0; i < 8; ++i)
        lenbuf[i] = static_cast<unsigned char>((bit_len >> (56 - 8 * i)) & 0xFF);
    sha256_update(c, lenbuf, 8);  // triggers the final compression
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<unsigned char>((c.h[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<unsigned char>((c.h[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<unsigned char>((c.h[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<unsigned char>(c.h[i] & 0xFF);
    }
}

// ===========================================================================
// HMAC-SHA256 (RFC 2104)
// ===========================================================================

void hmac_sha256(const unsigned char* key, std::size_t key_len,
                 const unsigned char* msg, std::size_t msg_len,
                 unsigned char out[32]) {
    unsigned char k0[64];
    std::memset(k0, 0, sizeof(k0));
    if (key_len > 64) {
        Sha256Ctx kc;
        sha256_init(kc);
        sha256_update(kc, key, key_len);
        sha256_final(kc, k0);  // first 32 bytes; rest stay zero
    } else {
        std::memcpy(k0, key, key_len);
    }
    unsigned char ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<unsigned char>(k0[i] ^ 0x36);
        opad[i] = static_cast<unsigned char>(k0[i] ^ 0x5c);
    }
    unsigned char inner[32];
    Sha256Ctx ic;
    sha256_init(ic);
    sha256_update(ic, ipad, 64);
    sha256_update(ic, msg, msg_len);
    sha256_final(ic, inner);

    Sha256Ctx oc;
    sha256_init(oc);
    sha256_update(oc, opad, 64);
    sha256_update(oc, inner, 32);
    sha256_final(oc, out);
}

// ===========================================================================
// AES-256 block cipher (FIPS 197) — key expansion + encrypt block
// ===========================================================================

const unsigned char kSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};

// AES-256: 14 rounds, 60 words (240 bytes) of round-key schedule.
struct Aes256 {
    unsigned char round_keys[240];
};

unsigned char xtime(unsigned char x) {
    return static_cast<unsigned char>((x << 1) ^ ((x >> 7) * 0x1b));
}

void aes256_key_expand(Aes256& a, const unsigned char key[32]) {
    // Nk = 8 words, Nr = 14 rounds -> 4*(Nr+1) = 60 words.
    std::memcpy(a.round_keys, key, 32);
    unsigned char rcon = 1;
    for (int i = 8; i < 60; ++i) {
        unsigned char t[4];
        const int prev = (i - 1) * 4;
        t[0] = a.round_keys[prev];
        t[1] = a.round_keys[prev + 1];
        t[2] = a.round_keys[prev + 2];
        t[3] = a.round_keys[prev + 3];
        if (i % 8 == 0) {
            // RotWord + SubWord + Rcon.
            const unsigned char tmp = t[0];
            t[0] = kSbox[t[1]] ^ rcon;
            t[1] = kSbox[t[2]];
            t[2] = kSbox[t[3]];
            t[3] = kSbox[tmp];
            rcon = xtime(rcon);
        } else if (i % 8 == 4) {
            // SubWord only (extra step for 256-bit keys).
            t[0] = kSbox[t[0]];
            t[1] = kSbox[t[1]];
            t[2] = kSbox[t[2]];
            t[3] = kSbox[t[3]];
        }
        const int cur = i * 4, back = (i - 8) * 4;
        a.round_keys[cur] = a.round_keys[back] ^ t[0];
        a.round_keys[cur + 1] = a.round_keys[back + 1] ^ t[1];
        a.round_keys[cur + 2] = a.round_keys[back + 2] ^ t[2];
        a.round_keys[cur + 3] = a.round_keys[back + 3] ^ t[3];
    }
}

void add_round_key(unsigned char state[16], const unsigned char* rk) {
    for (int i = 0; i < 16; ++i) state[i] ^= rk[i];
}

void sub_bytes(unsigned char state[16]) {
    for (int i = 0; i < 16; ++i) state[i] = kSbox[state[i]];
}

// State is column-major (FIPS 197): byte index = row + 4*col.
void shift_rows(unsigned char s[16]) {
    unsigned char t;
    // Row 1: shift left by 1.
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    // Row 2: shift left by 2.
    t = s[2]; s[2] = s[10]; s[10] = t;
    t = s[6]; s[6] = s[14]; s[14] = t;
    // Row 3: shift left by 3 (== right by 1).
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

void mix_columns(unsigned char s[16]) {
    for (int c = 0; c < 4; ++c) {
        unsigned char* col = s + c * 4;
        const unsigned char a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        col[0] = static_cast<unsigned char>(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
        col[1] = static_cast<unsigned char>(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
        col[2] = static_cast<unsigned char>(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
        col[3] = static_cast<unsigned char>((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
    }
}

void aes256_encrypt_block(const Aes256& a, const unsigned char in[16],
                          unsigned char out[16]) {
    unsigned char state[16];
    std::memcpy(state, in, 16);
    add_round_key(state, a.round_keys);  // round 0
    for (int round = 1; round < 14; ++round) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, a.round_keys + round * 16);
    }
    sub_bytes(state);   // final round: no MixColumns
    shift_rows(state);
    add_round_key(state, a.round_keys + 14 * 16);
    std::memcpy(out, state, 16);
}

// ===========================================================================
// AES-GCM (NIST SP 800-38D): CTR encryption + GHASH authentication
// ===========================================================================

// 128-bit value as two 64-bit halves (big-endian semantics for GF(2^128)).
struct Block128 {
    std::uint64_t hi;
    std::uint64_t lo;
};

Block128 load_be128(const unsigned char b[16]) {
    Block128 r;
    r.hi = 0;
    r.lo = 0;
    for (int i = 0; i < 8; ++i) r.hi = (r.hi << 8) | b[i];
    for (int i = 8; i < 16; ++i) r.lo = (r.lo << 8) | b[i];
    return r;
}

void store_be128(const Block128& v, unsigned char b[16]) {
    for (int i = 0; i < 8; ++i)
        b[i] = static_cast<unsigned char>((v.hi >> (56 - 8 * i)) & 0xFF);
    for (int i = 0; i < 8; ++i)
        b[8 + i] = static_cast<unsigned char>((v.lo >> (56 - 8 * i)) & 0xFF);
}

// GF(2^128) multiplication per SP 800-38D: the field uses the reduction
// polynomial x^128 + x^7 + x^2 + x + 1, with bit 0 of byte 0 the most
// significant. We process X's bits MSB-first; on each step, if the current bit
// is set we XOR V into Z, then shift V right by one and reduce with 0xe1 when a
// bit falls off the bottom.
Block128 gf_mult(const Block128& X, const Block128& Y) {
    Block128 Z{0, 0};
    Block128 V = Y;
    for (int i = 0; i < 128; ++i) {
        // i-th bit of X, MSB-first (bit 127 down to 0).
        const std::uint64_t bit =
            (i < 64) ? ((X.hi >> (63 - i)) & 1u) : ((X.lo >> (63 - (i - 64))) & 1u);
        if (bit) {
            Z.hi ^= V.hi;
            Z.lo ^= V.lo;
        }
        // V >>= 1 across the 128-bit value.
        const std::uint64_t lsb = V.lo & 1u;
        V.lo = (V.lo >> 1) | (V.hi << 63);
        V.hi >>= 1;
        if (lsb) V.hi ^= 0xe100000000000000ULL;  // reduction (R = 0xe1 << 120)
    }
    return Z;
}

// Incremental GHASH: y <- (y XOR block) * H.
void ghash_block(Block128& y, const Block128& H, const unsigned char block[16]) {
    const Block128 b = load_be128(block);
    y.hi ^= b.hi;
    y.lo ^= b.lo;
    y = gf_mult(y, H);
}

// Feeds `len` bytes through GHASH, zero-padding the final partial block to 16.
void ghash_bytes(Block128& y, const Block128& H, const unsigned char* data,
                 std::size_t len) {
    unsigned char blk[16];
    while (len >= 16) {
        ghash_block(y, H, data);
        data += 16;
        len -= 16;
    }
    if (len > 0) {
        std::memset(blk, 0, 16);
        std::memcpy(blk, data, len);
        ghash_block(y, H, blk);
    }
}

// Increments the rightmost 32 bits of the counter block (GCM's inc32).
void inc32(unsigned char ctr[16]) {
    for (int i = 15; i >= 12; --i) {
        if (++ctr[i] != 0) break;
    }
}

// Core GCM: produces ciphertext (CTR over J0+1, +2, ...) and the 16-byte tag.
// `encrypt` selects direction; for decrypt the caller passes the ciphertext as
// `in` and we GHASH that (the input ciphertext), matching the spec.
void gcm_crypt(const Aes256& aes, const unsigned char iv[12],
               const unsigned char* aad, std::size_t aad_len,
               const unsigned char* in, std::size_t in_len, bool encrypt,
               unsigned char* out, unsigned char tag[16]) {
    // H = AES_K(0^128).
    unsigned char zero[16];
    std::memset(zero, 0, 16);
    unsigned char Hbytes[16];
    aes256_encrypt_block(aes, zero, Hbytes);
    const Block128 H = load_be128(Hbytes);

    // J0 for a 96-bit IV = IV || 0x00000001.
    unsigned char J0[16];
    std::memcpy(J0, iv, 12);
    J0[12] = 0; J0[13] = 0; J0[14] = 0; J0[15] = 1;

    // CTR starts at J0 incremented once; encrypt each keystream block.
    unsigned char ctr[16];
    std::memcpy(ctr, J0, 16);
    unsigned char ks[16];
    std::size_t off = 0;
    while (off < in_len) {
        inc32(ctr);
        aes256_encrypt_block(aes, ctr, ks);
        const std::size_t n = std::min(std::size_t(16), in_len - off);
        for (std::size_t i = 0; i < n; ++i) out[off + i] = in[off + i] ^ ks[i];
        off += n;
    }

    // GHASH over AAD then ciphertext (for both directions the ciphertext is
    // authenticated): on encrypt that's `out`, on decrypt it's `in`.
    Block128 y{0, 0};
    ghash_bytes(y, H, aad, aad_len);
    ghash_bytes(y, H, encrypt ? out : in, in_len);
    // Length block: [len(AAD) in bits | len(C) in bits], each 64-bit big-endian.
    unsigned char lenblk[16];
    const std::uint64_t aad_bits = static_cast<std::uint64_t>(aad_len) * 8;
    const std::uint64_t c_bits = static_cast<std::uint64_t>(in_len) * 8;
    for (int i = 0; i < 8; ++i)
        lenblk[i] = static_cast<unsigned char>((aad_bits >> (56 - 8 * i)) & 0xFF);
    for (int i = 0; i < 8; ++i)
        lenblk[8 + i] = static_cast<unsigned char>((c_bits >> (56 - 8 * i)) & 0xFF);
    ghash_block(y, H, lenblk);

    // Tag = GHASH result XOR AES_K(J0).
    unsigned char ej0[16];
    aes256_encrypt_block(aes, J0, ej0);
    unsigned char ybytes[16];
    store_be128(y, ybytes);
    for (int i = 0; i < 16; ++i) tag[i] = ybytes[i] ^ ej0[i];
}

// ===========================================================================
// Base64 (standard alphabet, '=' padding)
// ===========================================================================

const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64_val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;  // padding or non-alphabet character
}

// ===========================================================================
// CSPRNG: std::random_device-seeded (never rand()).
// ===========================================================================

void fill_random(unsigned char* out, std::size_t n) {
    std::random_device rd;
    // Seed a Mersenne Twister from the OS entropy source, then draw bytes. The
    // seeded engine decouples us from random_device's potentially small/blocking
    // per-call cost while still taking nondeterministic entropy from the OS.
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = static_cast<unsigned char>(dist(gen));
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================

std::vector<unsigned char> sha256(const unsigned char* data, std::size_t len) {
    Sha256Ctx c;
    sha256_init(c);
    if (len > 0 && data != nullptr) sha256_update(c, data, len);
    std::vector<unsigned char> out(32);
    sha256_final(c, out.data());
    return out;
}

std::vector<unsigned char> pbkdf2_hmac_sha256(const std::string& password,
                                              const unsigned char* salt,
                                              std::size_t salt_len, int iterations,
                                              std::size_t dk_len) {
    std::vector<unsigned char> dk(dk_len);
    const unsigned char* pw = reinterpret_cast<const unsigned char*>(password.data());
    const std::size_t pw_len = password.size();

    // Each output block T_i = F(P, S, c, i): U1 = HMAC(P, S || INT(i)); subsequent
    // U_j = HMAC(P, U_{j-1}); T_i = U1 XOR U2 XOR ... XOR Uc.
    std::uint32_t block_index = 1;
    std::size_t done = 0;
    while (done < dk_len) {
        // Salt || big-endian block index.
        std::vector<unsigned char> salted(salt_len + 4);
        if (salt_len > 0 && salt != nullptr)
            std::memcpy(salted.data(), salt, salt_len);
        salted[salt_len] = static_cast<unsigned char>((block_index >> 24) & 0xFF);
        salted[salt_len + 1] = static_cast<unsigned char>((block_index >> 16) & 0xFF);
        salted[salt_len + 2] = static_cast<unsigned char>((block_index >> 8) & 0xFF);
        salted[salt_len + 3] = static_cast<unsigned char>(block_index & 0xFF);

        unsigned char u[32], t[32];
        hmac_sha256(pw, pw_len, salted.data(), salted.size(), u);
        std::memcpy(t, u, 32);
        for (int j = 1; j < iterations; ++j) {
            hmac_sha256(pw, pw_len, u, 32, u);
            for (int k = 0; k < 32; ++k) t[k] ^= u[k];
        }
        const std::size_t take = std::min(std::size_t(32), dk_len - done);
        std::memcpy(dk.data() + done, t, take);
        done += take;
        ++block_index;
    }
    return dk;
}

std::vector<unsigned char> aes256_gcm_encrypt(const unsigned char* key,
                                              const unsigned char* iv,
                                              const unsigned char* aad,
                                              std::size_t aad_len,
                                              const unsigned char* plaintext,
                                              std::size_t pt_len) {
    Aes256 aes;
    aes256_key_expand(aes, key);
    std::vector<unsigned char> out(pt_len + 16);  // ciphertext || tag
    unsigned char tag[16];
    gcm_crypt(aes, iv, aad, aad_len, plaintext, pt_len, /*encrypt=*/true,
              out.data(), tag);
    std::memcpy(out.data() + pt_len, tag, 16);
    return out;
}

std::vector<unsigned char> aes256_gcm_decrypt(const unsigned char* key,
                                              const unsigned char* iv,
                                              const unsigned char* aad,
                                              std::size_t aad_len,
                                              const unsigned char* ct_and_tag,
                                              std::size_t len) {
    if (len < 16) return {};  // not even room for the tag
    const std::size_t ct_len = len - 16;
    const unsigned char* recv_tag = ct_and_tag + ct_len;

    Aes256 aes;
    aes256_key_expand(aes, key);
    std::vector<unsigned char> pt(ct_len);
    unsigned char tag[16];
    gcm_crypt(aes, iv, aad, aad_len, ct_and_tag, ct_len, /*encrypt=*/false,
              pt.data(), tag);

    // Constant-time tag comparison; reject (return empty) on any mismatch.
    unsigned char diff = 0;
    for (int i = 0; i < 16; ++i) diff |= static_cast<unsigned char>(tag[i] ^ recv_tag[i]);
    if (diff != 0) return {};
    return pt;
}

std::string base64_encode(const unsigned char* data, std::size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= len) {
        const std::uint32_t n =
            (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8) |
            std::uint32_t(data[i + 2]);
        out += kB64[(n >> 18) & 0x3F];
        out += kB64[(n >> 12) & 0x3F];
        out += kB64[(n >> 6) & 0x3F];
        out += kB64[n & 0x3F];
        i += 3;
    }
    const std::size_t rem = len - i;
    if (rem == 1) {
        const std::uint32_t n = std::uint32_t(data[i]) << 16;
        out += kB64[(n >> 18) & 0x3F];
        out += kB64[(n >> 12) & 0x3F];
        out += '=';
        out += '=';
    } else if (rem == 2) {
        const std::uint32_t n =
            (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8);
        out += kB64[(n >> 18) & 0x3F];
        out += kB64[(n >> 12) & 0x3F];
        out += kB64[(n >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

std::vector<unsigned char> base64_decode(const std::string& s) {
    std::vector<unsigned char> out;
    out.reserve((s.size() / 4) * 3);
    int buf = 0, bits = 0;
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '=' ) break;                 // padding: stop accumulating
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;  // skip ws
        const int v = b64_val(c);
        if (v < 0) continue;                  // ignore stray non-alphabet bytes
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

EncryptedBlob encrypt_with_password(const std::string& password,
                                    const std::string& plaintext, int iterations) {
    unsigned char salt[16];
    unsigned char iv[12];
    fill_random(salt, sizeof(salt));
    fill_random(iv, sizeof(iv));

    const std::vector<unsigned char> key =
        pbkdf2_hmac_sha256(password, salt, sizeof(salt), iterations, 32);
    const std::vector<unsigned char> ct = aes256_gcm_encrypt(
        key.data(), iv, nullptr, 0,
        reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size());

    EncryptedBlob blob;
    blob.salt_b64 = base64_encode(salt, sizeof(salt));
    blob.iv_b64 = base64_encode(iv, sizeof(iv));
    blob.ciphertext_b64 = base64_encode(ct.data(), ct.size());
    blob.iterations = iterations;
    return blob;
}

std::string self_decrypting_html(const std::string& password,
                                 const std::string& inner_html, int iterations) {
    const EncryptedBlob blob = encrypt_with_password(password, inner_html, iterations);

    // A standalone page: it carries the ciphertext + envelope params and an
    // inline script that re-derives the key with Web Crypto. The JS mirrors the
    // C++ envelope exactly — PBKDF2-HMAC-SHA256(password, salt, iters, 256) then
    // AES-GCM decrypt with the 12-byte IV and the tag appended to the
    // ciphertext (Web Crypto expects exactly that concatenation).
    std::string html;
    html.reserve(inner_html.size() + 4096);
    html +=
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "<title>Encrypted export</title>\n"
        "<style>\n"
        "  :root{color-scheme:light dark}\n"
        "  body{font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;"
        "margin:0;background:#f4f4f7;color:#111}\n"
        "  #gate{max-width:420px;margin:14vh auto;padding:28px;background:#fff;"
        "border-radius:14px;box-shadow:0 8px 30px rgba(0,0,0,.12);text-align:center}\n"
        "  @media (prefers-color-scheme:dark){body{background:#0b0b0d;color:#eee}"
        "#gate{background:#17171a;box-shadow:0 8px 30px rgba(0,0,0,.6)}}\n"
        "  #gate h1{font-size:18px;margin:0 0 4px}\n"
        "  #gate p{font-size:13px;color:#888;margin:0 0 18px}\n"
        "  #pw{width:100%;box-sizing:border-box;padding:11px 12px;font-size:15px;"
        "border:1px solid #ccc;border-radius:8px;background:transparent;color:inherit}\n"
        "  #go{margin-top:12px;width:100%;padding:11px;font-size:15px;border:0;"
        "border-radius:8px;background:#0a84ff;color:#fff;cursor:pointer}\n"
        "  #go:disabled{opacity:.6;cursor:default}\n"
        "  #err{color:#e5484d;font-size:13px;min-height:18px;margin-top:10px}\n"
        "</style>\n</head>\n<body>\n"
        "<div id=\"gate\">\n"
        "  <h1>This export is encrypted</h1>\n"
        "  <p>Enter the password to view it.</p>\n"
        "  <form id=\"f\">\n"
        "    <input id=\"pw\" type=\"password\" autocomplete=\"current-password\" "
        "autofocus placeholder=\"Password\">\n"
        "    <button id=\"go\" type=\"submit\">Unlock</button>\n"
        "    <div id=\"err\"></div>\n"
        "  </form>\n"
        "</div>\n"
        "<script>\n"
        "(function(){\n"
        "  var SALT_B64=\"" + blob.salt_b64 + "\";\n"
        "  var IV_B64=\"" + blob.iv_b64 + "\";\n"
        "  var CT_B64=\"" + blob.ciphertext_b64 + "\";\n"
        "  var ITER=" + std::to_string(blob.iterations) + ";\n"
        "  function b64ToBytes(b64){\n"
        "    var bin=atob(b64);var a=new Uint8Array(bin.length);\n"
        "    for(var i=0;i<bin.length;i++)a[i]=bin.charCodeAt(i);return a;\n"
        "  }\n"
        "  var salt=b64ToBytes(SALT_B64), iv=b64ToBytes(IV_B64), ct=b64ToBytes(CT_B64);\n"
        "  var enc=new TextEncoder(), dec=new TextDecoder();\n"
        "  async function decrypt(pw){\n"
        "    var base=await crypto.subtle.importKey('raw',enc.encode(pw),{name:'PBKDF2'},"
        "false,['deriveKey']);\n"
        "    var key=await crypto.subtle.deriveKey(\n"
        "      {name:'PBKDF2',salt:salt,iterations:ITER,hash:'SHA-256'},base,\n"
        "      {name:'AES-GCM',length:256},false,['decrypt']);\n"
        "    var pt=await crypto.subtle.decrypt({name:'AES-GCM',iv:iv},key,ct);\n"
        "    return dec.decode(pt);\n"
        "  }\n"
        "  function show(html){\n"
        "    document.open();document.write(html);document.close();\n"
        "  }\n"
        "  var f=document.getElementById('f'), pw=document.getElementById('pw'),\n"
        "      go=document.getElementById('go'), err=document.getElementById('err');\n"
        "  f.addEventListener('submit',async function(e){\n"
        "    e.preventDefault();err.textContent='';go.disabled=true;\n"
        "    try{ var html=await decrypt(pw.value); show(html); }\n"
        "    catch(ex){ err.textContent='Wrong password'; go.disabled=false;\n"
        "      pw.focus(); pw.select(); }\n"
        "  });\n"
        "})();\n"
        "</script>\n"
        "</body>\n</html>\n";
    return html;
}

}  // namespace imsg
