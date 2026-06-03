// Self-contained AES-256-GCM encryption for the SQLite-free core, designed so a
// browser's Web Crypto API (SubtleCrypto) can decrypt the output. Every
// primitive below is implemented from scratch in crypto.cpp (no OpenSSL, no
// SQLite) — SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256, the AES-256 block cipher,
// and AES-GCM (CTR + GHASH). Correctness is pinned by known-answer tests in
// tests/test_core.cpp; those vectors are the acceptance gate.
//
// The envelope (PBKDF2 -> AES-256-GCM, 16-byte salt, 12-byte IV, 16-byte tag
// appended to the ciphertext) is exactly what the inline JS in
// self_decrypting_html re-derives, so a password-protected HTML export decrypts
// in any modern browser with no server and no dependencies.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace imsg {

// --- Low-level, deterministic primitives (caller supplies salt/iv) ----------
// These take raw inputs and are exercised directly by the KATs.

// SHA-256 (FIPS 180-4). Returns the 32-byte digest.
std::vector<unsigned char> sha256(const unsigned char* data, std::size_t len);

// PBKDF2-HMAC-SHA256 (RFC 8018). Derives `dk_len` bytes from the password and
// salt over `iterations` rounds.
std::vector<unsigned char> pbkdf2_hmac_sha256(const std::string& password,
                                              const unsigned char* salt,
                                              std::size_t salt_len, int iterations,
                                              std::size_t dk_len);

// AES-256-GCM (NIST SP 800-38D). `key` must be 32 bytes, `iv` 12 bytes; `aad`
// may be empty (pass len 0). Returns the ciphertext with the 16-byte
// authentication tag APPENDED — the exact byte layout Web Crypto's decrypt()
// expects.
std::vector<unsigned char> aes256_gcm_encrypt(const unsigned char* key,
                                              const unsigned char* iv,
                                              const unsigned char* aad,
                                              std::size_t aad_len,
                                              const unsigned char* plaintext,
                                              std::size_t pt_len);

// Inverse of aes256_gcm_encrypt (for round-trip tests). `ct_and_tag` is the
// ciphertext with the 16-byte tag appended. Returns the recovered plaintext, or
// an empty vector on authentication failure (tag mismatch) or malformed input.
std::vector<unsigned char> aes256_gcm_decrypt(const unsigned char* key,
                                              const unsigned char* iv,
                                              const unsigned char* aad,
                                              std::size_t aad_len,
                                              const unsigned char* ct_and_tag,
                                              std::size_t len);

// --- High-level password envelope -------------------------------------------

// The base64'd pieces of one password-encrypted payload, plus the PBKDF2 round
// count. salt is 16 bytes, iv 12 bytes; ciphertext_b64 holds the AES-GCM output
// with the tag appended.
struct EncryptedBlob {
    std::string salt_b64;
    std::string iv_b64;
    std::string ciphertext_b64;
    int iterations = 0;
};

// Generates a random 16-byte salt and 12-byte IV (std::random_device-seeded
// CSPRNG — never rand()), derives a 32-byte key via PBKDF2-HMAC-SHA256 with
// `iterations`, encrypts `plaintext` with AES-256-GCM, and returns the pieces
// base64-encoded.
EncryptedBlob encrypt_with_password(const std::string& password,
                                    const std::string& plaintext,
                                    int iterations = 250000);

// Wraps `inner_html` into a standalone, self-decrypting .html page: it embeds
// the base64 ciphertext plus an inline <script> that prompts for the password
// and decrypts in-browser via window.crypto.subtle (PBKDF2 -> AES-GCM), then
// renders the recovered HTML. On a wrong password it shows "Wrong password".
// The JS mirrors the C++ envelope byte-for-byte. Returns the full HTML string.
std::string self_decrypting_html(const std::string& password,
                                 const std::string& inner_html,
                                 int iterations = 250000);

// --- Base64 (standard alphabet, '=' padding) --------------------------------
std::string base64_encode(const unsigned char* data, std::size_t len);
std::vector<unsigned char> base64_decode(const std::string& s);

}  // namespace imsg
