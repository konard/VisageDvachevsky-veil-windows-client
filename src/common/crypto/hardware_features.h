#pragma once

#include <cstdint>

namespace veil::crypto {

// CPU feature flags for hardware acceleration.
// Detected at runtime via CPUID (x86/x64) or equivalent instructions.
struct CpuFeatures {
  // x86/x64 SIMD features
  bool has_sse2 = false;      // SSE2 (required for most SIMD)
  bool has_sse41 = false;     // SSE4.1 (additional SIMD instructions)
  bool has_sse42 = false;     // SSE4.2 (CRC32, string instructions)
  bool has_avx = false;       // AVX (256-bit SIMD)
  bool has_avx2 = false;      // AVX2 (256-bit integer SIMD)
  bool has_avx512f = false;   // AVX-512 Foundation

  // Hardware crypto acceleration
  bool has_aesni = false;     // AES-NI (hardware AES)
  bool has_pclmulqdq = false; // Carry-less multiplication (for GCM)
  bool has_sha = false;       // SHA extensions

  // ARM features (for future cross-platform support)
  bool has_neon = false;      // ARM NEON SIMD
  bool has_aes_arm = false;   // ARM AES instructions
  bool has_sha_arm = false;   // ARM SHA instructions
};

// Get the detected CPU features. This is thread-safe and caches the result.
// The detection is performed once on first call.
const CpuFeatures& get_cpu_features() noexcept;

// Check if hardware AES acceleration is available.
// Returns true if AES-NI (x86/x64) or AES instructions (ARM) are available.
inline bool has_hardware_aes() noexcept {
  const auto& features = get_cpu_features();
  return features.has_aesni || features.has_aes_arm;
}

// Check if hardware AES-GCM acceleration is available.
// Requires both AES-NI and PCLMULQDQ for efficient GCM mode.
inline bool has_hardware_aes_gcm() noexcept {
  const auto& features = get_cpu_features();
  return (features.has_aesni && features.has_pclmulqdq) || features.has_aes_arm;
}

// Check if AVX2 is available for vectorized operations.
inline bool has_avx2() noexcept {
  return get_cpu_features().has_avx2;
}

// Get a human-readable string describing the detected CPU features.
// Useful for logging/diagnostics.
const char* get_cpu_features_string() noexcept;

}  // namespace veil::crypto
