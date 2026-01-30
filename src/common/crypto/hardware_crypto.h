#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "common/crypto/crypto_engine.h"

namespace veil::crypto {

// ============================================================================
// Hardware-Accelerated Sequence Obfuscation
// ============================================================================

// Obfuscate sequence number using AES-NI when available.
// Falls back to ChaCha20-based obfuscation when AES-NI is not available.
// This provides ~10x speedup on systems with AES-NI support.
//
// The obfuscation uses a single AES block encryption as a pseudorandom function.
// This is cryptographically secure when the key is secret and produces
// indistinguishable-from-random output for DPI resistance.
std::uint64_t obfuscate_sequence_hw(std::uint64_t sequence,
                                     std::span<const std::uint8_t, kAeadKeyLen> obfuscation_key);

// Deobfuscate sequence number using AES-NI when available.
// The obfuscation is designed to be symmetric (XOR-based), so this function
// simply calls obfuscate_sequence_hw internally.
std::uint64_t deobfuscate_sequence_hw(std::uint64_t obfuscated_sequence,
                                       std::span<const std::uint8_t, kAeadKeyLen> obfuscation_key);

// ============================================================================
// AES-GCM AEAD (Alternative to ChaCha20-Poly1305)
// ============================================================================

// AES-GCM constants
inline constexpr std::size_t kAesGcmKeyLen = 32;      // AES-256-GCM key size
inline constexpr std::size_t kAesGcmNonceLen = 12;    // GCM nonce size (96 bits)
inline constexpr std::size_t kAesGcmTagLen = 16;      // GCM authentication tag size

// Encrypt using AES-GCM when hardware AES is available.
// Falls back to ChaCha20-Poly1305 when hardware AES is not available.
// This provides ~15x speedup for bulk encryption on systems with AES-NI.
//
// Parameters match the ChaCha20-Poly1305 API for drop-in replacement.
std::vector<std::uint8_t> aead_encrypt_hw(std::span<const std::uint8_t, kAeadKeyLen> key,
                                           std::span<const std::uint8_t, kNonceLen> nonce,
                                           std::span<const std::uint8_t> aad,
                                           std::span<const std::uint8_t> plaintext);

// Decrypt using AES-GCM when hardware AES is available.
// Falls back to ChaCha20-Poly1305 when hardware AES is not available.
std::optional<std::vector<std::uint8_t>> aead_decrypt_hw(
    std::span<const std::uint8_t, kAeadKeyLen> key,
    std::span<const std::uint8_t, kNonceLen> nonce,
    std::span<const std::uint8_t> aad,
    std::span<const std::uint8_t> ciphertext);

// Output buffer variants (avoid allocations in hot path)
std::size_t aead_encrypt_hw_to(std::span<const std::uint8_t, kAeadKeyLen> key,
                                std::span<const std::uint8_t, kNonceLen> nonce,
                                std::span<const std::uint8_t> aad,
                                std::span<const std::uint8_t> plaintext,
                                std::span<std::uint8_t> output);

std::size_t aead_decrypt_hw_to(std::span<const std::uint8_t, kAeadKeyLen> key,
                                std::span<const std::uint8_t, kNonceLen> nonce,
                                std::span<const std::uint8_t> aad,
                                std::span<const std::uint8_t> ciphertext,
                                std::span<std::uint8_t> output);

// ============================================================================
// Algorithm Selection
// ============================================================================

// AEAD algorithm types
enum class AeadAlgorithm : std::uint8_t {
  kChaCha20Poly1305 = 0,  // Default: ChaCha20-Poly1305 (libsodium)
  kAesGcm = 1,            // AES-256-GCM (when hardware AES available)
  kAuto = 255             // Auto-select based on hardware features
};

// Get the recommended AEAD algorithm for this system.
// Returns kAesGcm if hardware AES is available, otherwise kChaCha20Poly1305.
AeadAlgorithm get_recommended_aead_algorithm() noexcept;

// Get the algorithm name as a string for logging/diagnostics.
const char* aead_algorithm_name(AeadAlgorithm algo) noexcept;

// ============================================================================
// Unified AEAD API with Algorithm Selection
// ============================================================================

// Encrypt with the specified algorithm.
// If algorithm is kAuto, selects the best available algorithm for this system.
std::vector<std::uint8_t> aead_encrypt_with_algorithm(
    std::span<const std::uint8_t, kAeadKeyLen> key,
    std::span<const std::uint8_t, kNonceLen> nonce,
    std::span<const std::uint8_t> aad,
    std::span<const std::uint8_t> plaintext,
    AeadAlgorithm algorithm);

// Decrypt with the specified algorithm.
// If algorithm is kAuto, selects the best available algorithm for this system.
std::optional<std::vector<std::uint8_t>> aead_decrypt_with_algorithm(
    std::span<const std::uint8_t, kAeadKeyLen> key,
    std::span<const std::uint8_t, kNonceLen> nonce,
    std::span<const std::uint8_t> aad,
    std::span<const std::uint8_t> ciphertext,
    AeadAlgorithm algorithm);

}  // namespace veil::crypto
