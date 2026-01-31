#include "common/crypto/hardware_features.h"

#include <cstring>
#include <mutex>
#include <string>

// Platform-specific CPUID detection
#if defined(_MSC_VER)
  #include <intrin.h>
  #if defined(_M_X64) || defined(_M_IX86)
    #define VEIL_HAS_CPUID 1
  #endif
  // Clang on Windows also defines _MSC_VER; include <cpuid.h> so
  // the GCC/Clang-style __cpuid() wrapper is available.
  #if defined(__clang__) && (defined(__x86_64__) || defined(__i386__))
    #include <cpuid.h>
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #if defined(__x86_64__) || defined(__i386__)
    #include <cpuid.h>
    #define VEIL_HAS_CPUID 1
  #elif defined(__aarch64__) || defined(__arm__)
    // ARM: Use getauxval for feature detection on Linux
    #if defined(__linux__)
      #include <sys/auxv.h>
      #ifndef HWCAP_AES
        #define HWCAP_AES (1 << 3)
      #endif
      #ifndef HWCAP_SHA1
        #define HWCAP_SHA1 (1 << 5)
      #endif
      #ifndef HWCAP_SHA2
        #define HWCAP_SHA2 (1 << 6)
      #endif
      #ifndef HWCAP_ASIMD
        #define HWCAP_ASIMD (1 << 1)
      #endif
      #define VEIL_HAS_ARM_HWCAP 1
    #endif
  #endif
#endif

namespace veil::crypto {

namespace {

#if defined(VEIL_HAS_CPUID)

#if defined(__clang__) || defined(__GNUC__)
// GCC/Clang CPUID wrapper (must check before _MSC_VER since Clang on
// Windows defines both __clang__ and _MSC_VER)
void cpuid(int info[4], int function_id) {
  __cpuid(function_id,
          reinterpret_cast<unsigned int&>(info[0]),
          reinterpret_cast<unsigned int&>(info[1]),
          reinterpret_cast<unsigned int&>(info[2]),
          reinterpret_cast<unsigned int&>(info[3]));
}

void cpuidex(int info[4], int function_id, int subfunction_id) {
  __cpuid_count(function_id, subfunction_id,
                reinterpret_cast<unsigned int&>(info[0]),
                reinterpret_cast<unsigned int&>(info[1]),
                reinterpret_cast<unsigned int&>(info[2]),
                reinterpret_cast<unsigned int&>(info[3]));
}

std::uint64_t xgetbv(unsigned int index) {
  std::uint32_t eax = 0;
  std::uint32_t edx = 0;
  __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
  return (static_cast<std::uint64_t>(edx) << 32) | eax;
}
#elif defined(_MSC_VER)
// MSVC CPUID wrapper
void cpuid(int info[4], int function_id) {
  __cpuid(info, function_id);
}

void cpuidex(int info[4], int function_id, int subfunction_id) {
  __cpuidex(info, function_id, subfunction_id);
}

std::uint64_t xgetbv(unsigned int index) {
  return _xgetbv(index);
}
#endif  // _MSC_VER

CpuFeatures detect_x86_features() {
  CpuFeatures features{};
  int info[4] = {0};

  // Get highest supported CPUID function
  cpuid(info, 0);
  const int max_id = info[0];

  if (max_id < 1) {
    return features;
  }

  // CPUID function 1: Basic feature flags
  cpuid(info, 1);
  const std::uint32_t ecx1 = static_cast<std::uint32_t>(info[2]);
  const std::uint32_t edx1 = static_cast<std::uint32_t>(info[3]);

  // EDX flags
  features.has_sse2 = (edx1 & (1U << 26)) != 0;

  // ECX flags
  features.has_sse41 = (ecx1 & (1U << 19)) != 0;
  features.has_sse42 = (ecx1 & (1U << 20)) != 0;
  features.has_aesni = (ecx1 & (1U << 25)) != 0;
  features.has_pclmulqdq = (ecx1 & (1U << 1)) != 0;

  // Check for OSXSAVE (required to use AVX safely)
  const bool has_osxsave = (ecx1 & (1U << 27)) != 0;
  const bool has_avx_cpuid = (ecx1 & (1U << 28)) != 0;

  // AVX requires OS support (check XCR0)
  if (has_osxsave && has_avx_cpuid) {
    const std::uint64_t xcr0 = xgetbv(0);
    // Check if XMM and YMM state are enabled by OS
    const bool os_avx_support = ((xcr0 & 0x6) == 0x6);
    if (os_avx_support) {
      features.has_avx = true;

      // CPUID function 7: Extended feature flags (for AVX2, AVX-512, SHA)
      if (max_id >= 7) {
        cpuidex(info, 7, 0);
        const std::uint32_t ebx7 = static_cast<std::uint32_t>(info[1]);

        features.has_avx2 = (ebx7 & (1U << 5)) != 0;
        features.has_sha = (ebx7 & (1U << 29)) != 0;

        // AVX-512 requires additional OS support
        const bool os_avx512_support = ((xcr0 & 0xE6) == 0xE6);
        if (os_avx512_support) {
          features.has_avx512f = (ebx7 & (1U << 16)) != 0;
        }
      }
    }
  }

  return features;
}

#endif  // VEIL_HAS_CPUID

#if defined(VEIL_HAS_ARM_HWCAP)

CpuFeatures detect_arm_features() {
  CpuFeatures features{};

  const unsigned long hwcap = getauxval(AT_HWCAP);

  features.has_neon = (hwcap & HWCAP_ASIMD) != 0;
  features.has_aes_arm = (hwcap & HWCAP_AES) != 0;
  features.has_sha_arm = ((hwcap & HWCAP_SHA1) != 0) || ((hwcap & HWCAP_SHA2) != 0);

  return features;
}

#endif  // VEIL_HAS_ARM_HWCAP

CpuFeatures detect_cpu_features() {
#if defined(VEIL_HAS_CPUID)
  return detect_x86_features();
#elif defined(VEIL_HAS_ARM_HWCAP)
  return detect_arm_features();
#else
  // Unknown platform: return empty features (software fallback only)
  return CpuFeatures{};
#endif
}

std::string build_features_string(const CpuFeatures& features) {
  std::string result;

#if defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)
  result += "x86";
  if (features.has_sse2) result += " SSE2";
  if (features.has_sse41) result += " SSE4.1";
  if (features.has_sse42) result += " SSE4.2";
  if (features.has_avx) result += " AVX";
  if (features.has_avx2) result += " AVX2";
  if (features.has_avx512f) result += " AVX-512";
  if (features.has_aesni) result += " AES-NI";
  if (features.has_pclmulqdq) result += " PCLMULQDQ";
  if (features.has_sha) result += " SHA";
#elif defined(__aarch64__) || defined(__arm__) || defined(_M_ARM) || defined(_M_ARM64)
  result += "ARM";
  if (features.has_neon) result += " NEON";
  if (features.has_aes_arm) result += " AES";
  if (features.has_sha_arm) result += " SHA";
#else
  result += "Unknown";
#endif

  if (result.empty() || result == "x86" || result == "ARM" || result == "Unknown") {
    result += " (no hardware crypto)";
  }

  return result;
}

// Thread-safe singleton for CPU features
struct FeaturesHolder {
  CpuFeatures features;
  std::string features_string;

  FeaturesHolder() : features(detect_cpu_features()), features_string(build_features_string(features)) {}
};

const FeaturesHolder& get_features_holder() {
  static FeaturesHolder holder;
  return holder;
}

}  // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
const CpuFeatures& get_cpu_features() noexcept {
  return get_features_holder().features;
}

const char* get_cpu_features_string() noexcept {
  return get_features_holder().features_string.c_str();
}

}  // namespace veil::crypto
