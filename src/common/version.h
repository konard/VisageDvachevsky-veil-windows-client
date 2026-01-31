#pragma once

// VEIL VPN Version Information
// This header provides compile-time version constants used throughout the application.

namespace veil {

// Version numbers - update these when releasing new versions
constexpr int kVersionMajor = 1;
constexpr int kVersionMinor = 0;
constexpr int kVersionPatch = 0;

// Version string for display
constexpr const char* kVersionString = "1.0.0";

// Full version string with product name
constexpr const char* kFullVersionString = "VEIL VPN 1.0.0";

// GitHub repository for update checks
constexpr const char* kGitHubRepo = "VisageDvachevsky/veil-core";
constexpr const char* kGitHubReleasesApi = "https://api.github.com/repos/VisageDvachevsky/veil-core/releases/latest";
constexpr const char* kGitHubReleasesUrl = "https://github.com/VisageDvachevsky/veil-core/releases";

// Build information (can be overridden at compile time)
#ifndef VEIL_BUILD_TYPE
#define VEIL_BUILD_TYPE "Release"
#endif

#ifndef VEIL_GIT_HASH
#define VEIL_GIT_HASH "unknown"
#endif

constexpr const char* kBuildType = VEIL_BUILD_TYPE;
constexpr const char* kGitHash = VEIL_GIT_HASH;

}  // namespace veil
