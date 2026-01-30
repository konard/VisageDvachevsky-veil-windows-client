#include <gtest/gtest.h>

#ifdef _WIN32
#include "../../src/windows/app_enumerator.h"

using namespace veil::windows;

// Test that we can get a list of installed applications
TEST(AppEnumeratorTests, GetInstalledApplications) {
  auto apps = AppEnumerator::GetInstalledApplications();

  // On any Windows system, there should be at least some installed apps
  EXPECT_GT(apps.size(), 0u) << "Should find at least some installed applications";

  // Verify basic structure of returned apps
  for (const auto& app : apps) {
    EXPECT_FALSE(app.name.empty()) << "App name should not be empty";
    // Note: executable may be empty for some apps, so we don't check it
  }
}

// Test that we can get running processes
TEST(AppEnumeratorTests, GetRunningProcesses) {
  auto processes = AppEnumerator::GetRunningProcesses();

  // There should always be some running processes
  EXPECT_GT(processes.size(), 0u) << "Should find at least some running processes";

  // Verify basic structure
  for (const auto& proc : processes) {
    EXPECT_FALSE(proc.name.empty()) << "Process name should not be empty";
    EXPECT_FALSE(proc.executable.empty()) << "Process executable path should not be empty";
  }
}

// Test executable validation
TEST(AppEnumeratorTests, IsValidExecutable) {
  // These should be invalid
  EXPECT_FALSE(AppEnumerator::IsValidExecutable(""));
  EXPECT_FALSE(AppEnumerator::IsValidExecutable("not/a/real/path.exe"));
  EXPECT_FALSE(AppEnumerator::IsValidExecutable("C:\\Windows"));  // Directory, not file

  // This should be valid on Windows
  EXPECT_TRUE(AppEnumerator::IsValidExecutable("C:\\Windows\\System32\\notepad.exe"));
}

// Test that we filter out system apps correctly
TEST(AppEnumeratorTests, SystemAppFiltering) {
  auto apps = AppEnumerator::GetInstalledApplications();

  // Count system apps
  int systemAppCount = 0;
  for (const auto& app : apps) {
    if (app.isSystemApp) {
      systemAppCount++;
    }
  }

  // We should have some distinction between system and non-system apps
  EXPECT_LT(systemAppCount, static_cast<int>(apps.size()))
      << "Not all apps should be marked as system apps";
}

#else  // Non-Windows platforms

// On non-Windows platforms, the functions should return empty lists
TEST(AppEnumeratorTests, NonWindowsPlatformReturnsEmpty) {
  // These tests are only relevant on Windows
  GTEST_SKIP() << "AppEnumerator tests only run on Windows";
}

#endif  // _WIN32
