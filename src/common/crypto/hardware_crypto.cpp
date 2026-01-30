#include "common/crypto/hardware_crypto.h"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include "common/crypto/crypto_engine.h"
#include "common/crypto/hardware_features.h"

// Include platform-specific intrinsics for AES-NI
#if defined(_MSC_VER)
  #include <intrin.h>
  #if defined(_M_X64) || defined(_M_IX86)
    #include <wmmintrin.h>  // AES-NI
    #include <emmintrin.h>  // SSE2
    #define VEIL_HAS_AES_INTRINSICS 1
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #if defined(__x86_64__) || defined(__i386__)
    #if defined(__AES__) && defined(__SSE2__)
      #include <wmmintrin.h>  // AES-NI
      #include <emmintrin.h>  // SSE2
      #define VEIL_HAS_AES_INTRINSICS 1
    #else
      // AES intrinsics not available at compile time, but we may detect at runtime
      // We'll use runtime detection and software fallback
      #define VEIL_HAS_AES_INTRINSICS 0
    #endif
  #endif
#endif

#ifndef VEIL_HAS_AES_INTRINSICS
  #define VEIL_HAS_AES_INTRINSICS 0
#endif

namespace veil::crypto {

namespace {

// ============================================================================
// Software Implementation (Fallback)
// ============================================================================

// Software fallback uses the existing ChaCha20-based implementation
std::uint64_t obfuscate_sequence_sw(std::uint64_t sequence,
                                     std::span<const std::uint8_t, kAeadKeyLen> obfuscation_key) {
  // Use the existing optimized ChaCha20-based implementation (from Issue #93)
  return obfuscate_sequence(sequence, obfuscation_key);
}

// ============================================================================
// AES-NI Implementation
// ============================================================================

#if VEIL_HAS_AES_INTRINSICS

// AES key expansion helper functions
// These expand a 256-bit key into 15 round keys for AES-256

inline __m128i aes_128_key_expansion_assist(__m128i temp1, __m128i temp2) {
  __m128i temp3;
  temp2 = _mm_shuffle_epi32(temp2, 0xFF);
  temp3 = _mm_slli_si128(temp1, 0x4);
  temp1 = _mm_xor_si128(temp1, temp3);
  temp3 = _mm_slli_si128(temp3, 0x4);
  temp1 = _mm_xor_si128(temp1, temp3);
  temp3 = _mm_slli_si128(temp3, 0x4);
  temp1 = _mm_xor_si128(temp1, temp3);
  temp1 = _mm_xor_si128(temp1, temp2);
  return temp1;
}

inline __m128i aes_256_key_expansion_assist_1(__m128i temp1, __m128i temp2) {
  __m128i temp4;
  temp2 = _mm_shuffle_epi32(temp2, 0xFF);
  temp4 = _mm_slli_si128(temp1, 0x4);
  temp1 = _mm_xor_si128(temp1, temp4);
  temp4 = _mm_slli_si128(temp4, 0x4);
  temp1 = _mm_xor_si128(temp1, temp4);
  temp4 = _mm_slli_si128(temp4, 0x4);
  temp1 = _mm_xor_si128(temp1, temp4);
  temp1 = _mm_xor_si128(temp1, temp2);
  return temp1;
}

inline __m128i aes_256_key_expansion_assist_2(__m128i temp1, __m128i temp3) {
  __m128i temp2;
  __m128i temp4;
  temp4 = _mm_aeskeygenassist_si128(temp1, 0x0);
  temp2 = _mm_shuffle_epi32(temp4, 0xAA);
  temp4 = _mm_slli_si128(temp3, 0x4);
  temp3 = _mm_xor_si128(temp3, temp4);
  temp4 = _mm_slli_si128(temp4, 0x4);
  temp3 = _mm_xor_si128(temp3, temp4);
  temp4 = _mm_slli_si128(temp4, 0x4);
  temp3 = _mm_xor_si128(temp3, temp4);
  temp3 = _mm_xor_si128(temp3, temp2);
  return temp3;
}

// Expand a 256-bit key into round keys
void aes_256_key_expansion(const std::uint8_t* key, __m128i* round_keys) {
  __m128i temp1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key));
  __m128i temp3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key + 16));

  round_keys[0] = temp1;
  round_keys[1] = temp3;

  temp1 = aes_256_key_expansion_assist_1(temp1, _mm_aeskeygenassist_si128(temp3, 0x01));
  round_keys[2] = temp1;
  temp3 = aes_256_key_expansion_assist_2(temp1, temp3);
  round_keys[3] = temp3;

  temp1 = aes_256_key_expansion_assist_1(temp1, _mm_aeskeygenassist_si128(temp3, 0x02));
  round_keys[4] = temp1;
  temp3 = aes_256_key_expansion_assist_2(temp1, temp3);
  round_keys[5] = temp3;

  temp1 = aes_256_key_expansion_assist_1(temp1, _mm_aeskeygenassist_si128(temp3, 0x04));
  round_keys[6] = temp1;
  temp3 = aes_256_key_expansion_assist_2(temp1, temp3);
  round_keys[7] = temp3;

  temp1 = aes_256_key_expansion_assist_1(temp1, _mm_aeskeygenassist_si128(temp3, 0x08));
  round_keys[8] = temp1;
  temp3 = aes_256_key_expansion_assist_2(temp1, temp3);
  round_keys[9] = temp3;

  temp1 = aes_256_key_expansion_assist_1(temp1, _mm_aeskeygenassist_si128(temp3, 0x10));
  round_keys[10] = temp1;
  temp3 = aes_256_key_expansion_assist_2(temp1, temp3);
  round_keys[11] = temp3;

  temp1 = aes_256_key_expansion_assist_1(temp1, _mm_aeskeygenassist_si128(temp3, 0x20));
  round_keys[12] = temp1;
  temp3 = aes_256_key_expansion_assist_2(temp1, temp3);
  round_keys[13] = temp3;

  temp1 = aes_256_key_expansion_assist_1(temp1, _mm_aeskeygenassist_si128(temp3, 0x40));
  round_keys[14] = temp1;
}

// Single AES-256 block encryption (for sequence obfuscation)
__m128i aes_256_encrypt_block(__m128i block, const __m128i* round_keys) {
  block = _mm_xor_si128(block, round_keys[0]);
  block = _mm_aesenc_si128(block, round_keys[1]);
  block = _mm_aesenc_si128(block, round_keys[2]);
  block = _mm_aesenc_si128(block, round_keys[3]);
  block = _mm_aesenc_si128(block, round_keys[4]);
  block = _mm_aesenc_si128(block, round_keys[5]);
  block = _mm_aesenc_si128(block, round_keys[6]);
  block = _mm_aesenc_si128(block, round_keys[7]);
  block = _mm_aesenc_si128(block, round_keys[8]);
  block = _mm_aesenc_si128(block, round_keys[9]);
  block = _mm_aesenc_si128(block, round_keys[10]);
  block = _mm_aesenc_si128(block, round_keys[11]);
  block = _mm_aesenc_si128(block, round_keys[12]);
  block = _mm_aesenc_si128(block, round_keys[13]);
  block = _mm_aesenclast_si128(block, round_keys[14]);
  return block;
}

// AES-NI accelerated sequence obfuscation
// Uses AES as a pseudorandom function: F(key, seq) = AES_encrypt(key, seq || padding)
// Then XORs the original sequence with the output for symmetric obfuscation.
std::uint64_t obfuscate_sequence_aesni(std::uint64_t sequence,
                                        std::span<const std::uint8_t, kAeadKeyLen> obfuscation_key) {
  // Expand the key (this could be cached per-session for better performance)
  __m128i round_keys[15];
  aes_256_key_expansion(obfuscation_key.data(), round_keys);

  // Create input block: sequence (8 bytes) + domain separator (8 bytes)
  // Domain separator ensures this is distinct from other uses of the key
  alignas(16) std::array<std::uint8_t, 16> input{};
  std::memcpy(input.data(), &sequence, sizeof(sequence));
  // Fill remaining bytes with domain separator 'S' 'E' 'Q' 'O' 'B' 'F' 'S' 'C'
  input[8] = 'S';
  input[9] = 'E';
  input[10] = 'Q';
  input[11] = 'O';
  input[12] = 'B';
  input[13] = 'F';
  input[14] = 'S';
  input[15] = 'C';

  // Encrypt to get pseudorandom output
  __m128i block = _mm_load_si128(reinterpret_cast<const __m128i*>(input.data()));
  block = aes_256_encrypt_block(block, round_keys);

  // Extract first 8 bytes and XOR with original sequence
  // This makes the obfuscation symmetric (obfuscate == deobfuscate)
  alignas(16) std::array<std::uint8_t, 16> output{};
  _mm_store_si128(reinterpret_cast<__m128i*>(output.data()), block);

  std::uint64_t mask = 0;
  std::memcpy(&mask, output.data(), sizeof(mask));

  return sequence ^ mask;
}

#endif  // VEIL_HAS_AES_INTRINSICS

// ============================================================================
// libsodium AES-GCM implementation
// ============================================================================

// libsodium provides AES-GCM when hardware support is available
// It will automatically use AES-NI if present

bool is_aes_gcm_available() {
  // libsodium's crypto_aead_aes256gcm_is_available checks for AES-NI
  return crypto_aead_aes256gcm_is_available() != 0;
}

std::vector<std::uint8_t> aead_encrypt_aes_gcm(std::span<const std::uint8_t, kAeadKeyLen> key,
                                                std::span<const std::uint8_t, kNonceLen> nonce,
                                                std::span<const std::uint8_t> aad,
                                                std::span<const std::uint8_t> plaintext) {
  std::vector<std::uint8_t> ciphertext(plaintext.size() + crypto_aead_aes256gcm_ABYTES);
  unsigned long long out_len = 0;

  const auto rc = crypto_aead_aes256gcm_encrypt(
      ciphertext.data(), &out_len,
      plaintext.data(), plaintext.size(),
      aad.data(), aad.size(),
      nullptr,  // nsec (not used)
      nonce.data(),
      key.data());

  if (rc != 0) {
    return {};  // Encryption failed
  }

  ciphertext.resize(out_len);
  return ciphertext;
}

std::optional<std::vector<std::uint8_t>> aead_decrypt_aes_gcm(
    std::span<const std::uint8_t, kAeadKeyLen> key,
    std::span<const std::uint8_t, kNonceLen> nonce,
    std::span<const std::uint8_t> aad,
    std::span<const std::uint8_t> ciphertext) {
  if (ciphertext.size() < crypto_aead_aes256gcm_ABYTES) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> plaintext(ciphertext.size() - crypto_aead_aes256gcm_ABYTES);
  unsigned long long out_len = 0;

  const auto rc = crypto_aead_aes256gcm_decrypt(
      plaintext.data(), &out_len,
      nullptr,  // nsec (not used)
      ciphertext.data(), ciphertext.size(),
      aad.data(), aad.size(),
      nonce.data(),
      key.data());

  if (rc != 0) {
    return std::nullopt;  // Authentication failed
  }

  plaintext.resize(out_len);
  return plaintext;
}

std::size_t aead_encrypt_aes_gcm_to(std::span<const std::uint8_t, kAeadKeyLen> key,
                                     std::span<const std::uint8_t, kNonceLen> nonce,
                                     std::span<const std::uint8_t> aad,
                                     std::span<const std::uint8_t> plaintext,
                                     std::span<std::uint8_t> output) {
  const std::size_t required_size = plaintext.size() + crypto_aead_aes256gcm_ABYTES;
  if (output.size() < required_size) {
    return 0;
  }

  unsigned long long out_len = 0;
  const auto rc = crypto_aead_aes256gcm_encrypt(
      output.data(), &out_len,
      plaintext.data(), plaintext.size(),
      aad.data(), aad.size(),
      nullptr,
      nonce.data(),
      key.data());

  if (rc != 0) {
    return 0;
  }

  return static_cast<std::size_t>(out_len);
}

std::size_t aead_decrypt_aes_gcm_to(std::span<const std::uint8_t, kAeadKeyLen> key,
                                     std::span<const std::uint8_t, kNonceLen> nonce,
                                     std::span<const std::uint8_t> aad,
                                     std::span<const std::uint8_t> ciphertext,
                                     std::span<std::uint8_t> output) {
  if (ciphertext.size() < crypto_aead_aes256gcm_ABYTES) {
    return 0;
  }

  const std::size_t required_size = ciphertext.size() - crypto_aead_aes256gcm_ABYTES;
  if (output.size() < required_size) {
    return 0;
  }

  unsigned long long out_len = 0;
  const auto rc = crypto_aead_aes256gcm_decrypt(
      output.data(), &out_len,
      nullptr,
      ciphertext.data(), ciphertext.size(),
      aad.data(), aad.size(),
      nonce.data(),
      key.data());

  if (rc != 0) {
    return 0;
  }

  return static_cast<std::size_t>(out_len);
}

}  // namespace

// ============================================================================
// Public API Implementation
// ============================================================================

std::uint64_t obfuscate_sequence_hw(std::uint64_t sequence,
                                     std::span<const std::uint8_t, kAeadKeyLen> obfuscation_key) {
#if VEIL_HAS_AES_INTRINSICS
  if (has_hardware_aes()) {
    return obfuscate_sequence_aesni(sequence, obfuscation_key);
  }
#endif
  // Fallback to software implementation
  return obfuscate_sequence_sw(sequence, obfuscation_key);
}

std::uint64_t deobfuscate_sequence_hw(std::uint64_t obfuscated_sequence,
                                       std::span<const std::uint8_t, kAeadKeyLen> obfuscation_key) {
  // The obfuscation is XOR-based, so deobfuscation is identical
  return obfuscate_sequence_hw(obfuscated_sequence, obfuscation_key);
}

std::vector<std::uint8_t> aead_encrypt_hw(std::span<const std::uint8_t, kAeadKeyLen> key,
                                           std::span<const std::uint8_t, kNonceLen> nonce,
                                           std::span<const std::uint8_t> aad,
                                           std::span<const std::uint8_t> plaintext) {
  if (is_aes_gcm_available()) {
    return aead_encrypt_aes_gcm(key, nonce, aad, plaintext);
  }
  // Fallback to ChaCha20-Poly1305
  return aead_encrypt(key, nonce, aad, plaintext);
}

std::optional<std::vector<std::uint8_t>> aead_decrypt_hw(
    std::span<const std::uint8_t, kAeadKeyLen> key,
    std::span<const std::uint8_t, kNonceLen> nonce,
    std::span<const std::uint8_t> aad,
    std::span<const std::uint8_t> ciphertext) {
  if (is_aes_gcm_available()) {
    return aead_decrypt_aes_gcm(key, nonce, aad, ciphertext);
  }
  // Fallback to ChaCha20-Poly1305
  return aead_decrypt(key, nonce, aad, ciphertext);
}

std::size_t aead_encrypt_hw_to(std::span<const std::uint8_t, kAeadKeyLen> key,
                                std::span<const std::uint8_t, kNonceLen> nonce,
                                std::span<const std::uint8_t> aad,
                                std::span<const std::uint8_t> plaintext,
                                std::span<std::uint8_t> output) {
  if (is_aes_gcm_available()) {
    return aead_encrypt_aes_gcm_to(key, nonce, aad, plaintext, output);
  }
  // Fallback to ChaCha20-Poly1305
  return aead_encrypt_to(key, nonce, aad, plaintext, output);
}

std::size_t aead_decrypt_hw_to(std::span<const std::uint8_t, kAeadKeyLen> key,
                                std::span<const std::uint8_t, kNonceLen> nonce,
                                std::span<const std::uint8_t> aad,
                                std::span<const std::uint8_t> ciphertext,
                                std::span<std::uint8_t> output) {
  if (is_aes_gcm_available()) {
    return aead_decrypt_aes_gcm_to(key, nonce, aad, ciphertext, output);
  }
  // Fallback to ChaCha20-Poly1305
  return aead_decrypt_to(key, nonce, aad, ciphertext, output);
}

AeadAlgorithm get_recommended_aead_algorithm() noexcept {
  if (is_aes_gcm_available()) {
    return AeadAlgorithm::kAesGcm;
  }
  return AeadAlgorithm::kChaCha20Poly1305;
}

const char* aead_algorithm_name(AeadAlgorithm algo) noexcept {
  switch (algo) {
    case AeadAlgorithm::kChaCha20Poly1305:
      return "ChaCha20-Poly1305";
    case AeadAlgorithm::kAesGcm:
      return "AES-256-GCM";
    case AeadAlgorithm::kAuto:
      return "Auto";
  }
  return "Unknown";
}

std::vector<std::uint8_t> aead_encrypt_with_algorithm(
    std::span<const std::uint8_t, kAeadKeyLen> key,
    std::span<const std::uint8_t, kNonceLen> nonce,
    std::span<const std::uint8_t> aad,
    std::span<const std::uint8_t> plaintext,
    AeadAlgorithm algorithm) {
  // Resolve auto algorithm
  if (algorithm == AeadAlgorithm::kAuto) {
    algorithm = get_recommended_aead_algorithm();
  }

  switch (algorithm) {
    case AeadAlgorithm::kAesGcm:
      if (is_aes_gcm_available()) {
        return aead_encrypt_aes_gcm(key, nonce, aad, plaintext);
      }
      // Fallback if AES-GCM requested but not available
      [[fallthrough]];
    case AeadAlgorithm::kChaCha20Poly1305:
    default:
      return aead_encrypt(key, nonce, aad, plaintext);
  }
}

std::optional<std::vector<std::uint8_t>> aead_decrypt_with_algorithm(
    std::span<const std::uint8_t, kAeadKeyLen> key,
    std::span<const std::uint8_t, kNonceLen> nonce,
    std::span<const std::uint8_t> aad,
    std::span<const std::uint8_t> ciphertext,
    AeadAlgorithm algorithm) {
  // Resolve auto algorithm
  if (algorithm == AeadAlgorithm::kAuto) {
    algorithm = get_recommended_aead_algorithm();
  }

  switch (algorithm) {
    case AeadAlgorithm::kAesGcm:
      if (is_aes_gcm_available()) {
        return aead_decrypt_aes_gcm(key, nonce, aad, ciphertext);
      }
      // Fallback if AES-GCM requested but not available
      [[fallthrough]];
    case AeadAlgorithm::kChaCha20Poly1305:
    default:
      return aead_decrypt(key, nonce, aad, ciphertext);
  }
}

}  // namespace veil::crypto
