#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace veil::crypto {

inline constexpr std::size_t kX25519PublicKeySize = 32;
inline constexpr std::size_t kX25519SecretKeySize = 32;
inline constexpr std::size_t kSharedSecretSize = 32;
inline constexpr std::size_t kHmacSha256Len = 32;
inline constexpr std::size_t kAeadKeyLen = 32;
inline constexpr std::size_t kNonceLen = 12;

struct KeyPair {
  std::array<std::uint8_t, kX25519PublicKeySize> public_key{};
  std::array<std::uint8_t, kX25519SecretKeySize> secret_key{};
};

struct SessionKeys {
  std::array<std::uint8_t, kAeadKeyLen> send_key{};
  std::array<std::uint8_t, kAeadKeyLen> recv_key{};
  std::array<std::uint8_t, kNonceLen> send_nonce{};
  std::array<std::uint8_t, kNonceLen> recv_nonce{};
};

KeyPair generate_x25519_keypair();
std::array<std::uint8_t, kSharedSecretSize> compute_shared_secret(
    std::span<const std::uint8_t, kX25519SecretKeySize> secret_key,
    std::span<const std::uint8_t, kX25519PublicKeySize> peer_public);

std::vector<std::uint8_t> hmac_sha256(std::span<const std::uint8_t> key,
                                      std::span<const std::uint8_t> data);

std::array<std::uint8_t, kHmacSha256Len> hkdf_extract(std::span<const std::uint8_t> salt,
                                                      std::span<const std::uint8_t> ikm);
std::vector<std::uint8_t> hkdf_expand(std::span<const std::uint8_t, kHmacSha256Len> prk,
                                      std::span<const std::uint8_t> info, std::size_t length);

SessionKeys derive_session_keys(std::span<const std::uint8_t, kSharedSecretSize> shared_secret,
                                std::span<const std::uint8_t> salt, std::span<const std::uint8_t> info,
                                bool initiator);

std::array<std::uint8_t, kNonceLen> derive_nonce(
    std::span<const std::uint8_t, kNonceLen> base_nonce, std::uint64_t counter);

// Derive a key for sequence number obfuscation from session keys.
// This creates a deterministic but unique key per session for DPI resistance.
std::array<std::uint8_t, kAeadKeyLen> derive_sequence_obfuscation_key(
    std::span<const std::uint8_t, kAeadKeyLen> send_key,
    std::span<const std::uint8_t, kNonceLen> send_nonce);

// Obfuscate sequence number for transmission (sender side).
// Uses ChaCha20 stream cipher with the obfuscation key to make sequences indistinguishable from random.
// Optimized for performance (Issue #93) - replaces 3-round Feistel network with hardware-accelerated ChaCha20.
std::uint64_t obfuscate_sequence(std::uint64_t sequence,
                                  std::span<const std::uint8_t, kAeadKeyLen> obfuscation_key);

// Deobfuscate sequence number after reception (receiver side).
// Reverses the obfuscation to recover the original sequence for nonce derivation.
// ChaCha20 is symmetric, so deobfuscation is identical to obfuscation (XOR is its own inverse).
std::uint64_t deobfuscate_sequence(std::uint64_t obfuscated_sequence,
                                    std::span<const std::uint8_t, kAeadKeyLen> obfuscation_key);

std::vector<std::uint8_t> aead_encrypt(std::span<const std::uint8_t, kAeadKeyLen> key,
                                       std::span<const std::uint8_t, kNonceLen> nonce,
                                       std::span<const std::uint8_t> aad,
                                       std::span<const std::uint8_t> plaintext);
std::optional<std::vector<std::uint8_t>> aead_decrypt(
    std::span<const std::uint8_t, kAeadKeyLen> key, std::span<const std::uint8_t, kNonceLen> nonce,
    std::span<const std::uint8_t> aad, std::span<const std::uint8_t> ciphertext);

// PERFORMANCE (Issue #94): Output buffer variants that avoid allocations.
// These functions write to caller-provided buffers instead of allocating new vectors.
// The output buffer must have sufficient capacity.

// AEAD authentication tag size for ChaCha20-Poly1305.
inline constexpr std::size_t kAeadTagLen = 16;

// Calculate the required ciphertext buffer size for a given plaintext length.
inline constexpr std::size_t aead_ciphertext_size(std::size_t plaintext_len) noexcept {
  return plaintext_len + kAeadTagLen;
}

// Calculate the plaintext size from ciphertext length (returns 0 if ciphertext too small).
inline constexpr std::size_t aead_plaintext_size(std::size_t ciphertext_len) noexcept {
  return ciphertext_len >= kAeadTagLen ? ciphertext_len - kAeadTagLen : 0;
}

// Encrypt into a pre-allocated output buffer.
// The output buffer must have at least aead_ciphertext_size(plaintext.size()) bytes capacity.
// Returns the actual ciphertext size written (always plaintext.size() + kAeadTagLen on success).
// Returns 0 on failure (output buffer too small or encryption error).
std::size_t aead_encrypt_to(std::span<const std::uint8_t, kAeadKeyLen> key,
                            std::span<const std::uint8_t, kNonceLen> nonce,
                            std::span<const std::uint8_t> aad,
                            std::span<const std::uint8_t> plaintext,
                            std::span<std::uint8_t> output);

// Decrypt into a pre-allocated output buffer.
// The output buffer must have at least aead_plaintext_size(ciphertext.size()) bytes capacity.
// Returns the actual plaintext size written on success (always ciphertext.size() - kAeadTagLen).
// Returns 0 on failure (output buffer too small, authentication failure, or decryption error).
std::size_t aead_decrypt_to(std::span<const std::uint8_t, kAeadKeyLen> key,
                            std::span<const std::uint8_t, kNonceLen> nonce,
                            std::span<const std::uint8_t> aad,
                            std::span<const std::uint8_t> ciphertext,
                            std::span<std::uint8_t> output);

}  // namespace veil::crypto
