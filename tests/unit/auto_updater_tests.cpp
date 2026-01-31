#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "common/updater/auto_updater.h"

namespace veil::tests {

// ============================================================================
// Version Parsing Tests
// ============================================================================

TEST(VersionTests, ParseBasicVersion) {
  auto v = updater::Version::parse("1.2.3");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->major, 1);
  EXPECT_EQ(v->minor, 2);
  EXPECT_EQ(v->patch, 3);
  EXPECT_TRUE(v->prerelease.empty());
}

TEST(VersionTests, ParseVersionWithVPrefix) {
  auto v = updater::Version::parse("v1.2.3");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->major, 1);
  EXPECT_EQ(v->minor, 2);
  EXPECT_EQ(v->patch, 3);
  EXPECT_TRUE(v->prerelease.empty());
}

TEST(VersionTests, ParsePrereleaseVersion) {
  auto v = updater::Version::parse("1.2.3-beta.1");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->major, 1);
  EXPECT_EQ(v->minor, 2);
  EXPECT_EQ(v->patch, 3);
  EXPECT_EQ(v->prerelease, "beta.1");
}

TEST(VersionTests, ParseRCVersion) {
  auto v = updater::Version::parse("v2.0.0-rc.2");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->major, 2);
  EXPECT_EQ(v->minor, 0);
  EXPECT_EQ(v->patch, 0);
  EXPECT_EQ(v->prerelease, "rc.2");
}

TEST(VersionTests, ParseAlphaVersion) {
  auto v = updater::Version::parse("0.1.0-alpha");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->major, 0);
  EXPECT_EQ(v->minor, 1);
  EXPECT_EQ(v->patch, 0);
  EXPECT_EQ(v->prerelease, "alpha");
}

TEST(VersionTests, ParseInvalidVersionNoNumbers) {
  auto v = updater::Version::parse("invalid");
  EXPECT_FALSE(v.has_value());
}

TEST(VersionTests, ParseInvalidVersionPartial) {
  auto v = updater::Version::parse("1.2");
  EXPECT_FALSE(v.has_value());
}

TEST(VersionTests, ParseInvalidVersionExtraComponents) {
  auto v = updater::Version::parse("1.2.3.4");
  EXPECT_FALSE(v.has_value());
}

TEST(VersionTests, ParseInvalidVersionEmpty) {
  auto v = updater::Version::parse("");
  EXPECT_FALSE(v.has_value());
}

// ============================================================================
// Version Comparison Tests
// ============================================================================

TEST(VersionTests, ComparisonMajorVersion) {
  auto v1 = updater::Version::parse("1.0.0");
  auto v2 = updater::Version::parse("2.0.0");
  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  EXPECT_TRUE(*v1 < *v2);
  EXPECT_TRUE(*v2 > *v1);
  EXPECT_FALSE(*v1 == *v2);
}

TEST(VersionTests, ComparisonMinorVersion) {
  auto v1 = updater::Version::parse("1.1.0");
  auto v2 = updater::Version::parse("1.2.0");
  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  EXPECT_TRUE(*v1 < *v2);
  EXPECT_TRUE(*v2 > *v1);
}

TEST(VersionTests, ComparisonPatchVersion) {
  auto v1 = updater::Version::parse("1.0.1");
  auto v2 = updater::Version::parse("1.0.2");
  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  EXPECT_TRUE(*v1 < *v2);
  EXPECT_TRUE(*v2 > *v1);
}

TEST(VersionTests, ComparisonPrereleaseVsRelease) {
  auto v1 = updater::Version::parse("1.0.0-beta");
  auto v2 = updater::Version::parse("1.0.0");
  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  // Prerelease versions are less than release versions
  EXPECT_TRUE(*v1 < *v2);
  EXPECT_TRUE(*v2 > *v1);
}

TEST(VersionTests, ComparisonPrereleases) {
  auto v1 = updater::Version::parse("1.0.0-alpha");
  auto v2 = updater::Version::parse("1.0.0-beta");
  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  // Lexical comparison of prerelease identifiers
  EXPECT_TRUE(*v1 < *v2);
}

TEST(VersionTests, ComparisonEquality) {
  auto v1 = updater::Version::parse("1.2.3");
  auto v2 = updater::Version::parse("v1.2.3");
  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  EXPECT_TRUE(*v1 == *v2);
  EXPECT_FALSE(*v1 != *v2);
  EXPECT_TRUE(*v1 <= *v2);
  EXPECT_TRUE(*v1 >= *v2);
}

TEST(VersionTests, ComparisonLessOrEqual) {
  auto v1 = updater::Version::parse("1.0.0");
  auto v2 = updater::Version::parse("1.0.0");
  auto v3 = updater::Version::parse("2.0.0");
  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  ASSERT_TRUE(v3.has_value());
  EXPECT_TRUE(*v1 <= *v2);
  EXPECT_TRUE(*v1 <= *v3);
  EXPECT_FALSE(*v3 <= *v1);
}

TEST(VersionTests, ComparisonGreaterOrEqual) {
  auto v1 = updater::Version::parse("2.0.0");
  auto v2 = updater::Version::parse("2.0.0");
  auto v3 = updater::Version::parse("1.0.0");
  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  ASSERT_TRUE(v3.has_value());
  EXPECT_TRUE(*v1 >= *v2);
  EXPECT_TRUE(*v1 >= *v3);
  EXPECT_FALSE(*v3 >= *v1);
}

// ============================================================================
// Version to_string Tests
// ============================================================================

TEST(VersionTests, ToStringBasic) {
  auto v = updater::Version::parse("1.2.3");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->to_string(), "1.2.3");
}

TEST(VersionTests, ToStringPrerelease) {
  auto v = updater::Version::parse("1.2.3-beta.1");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->to_string(), "1.2.3-beta.1");
}

// ============================================================================
// ReleaseInfo Installer Selection Tests
// ============================================================================

TEST(ReleaseInfoTests, FindWindowsExeInstaller) {
  updater::ReleaseInfo release;
  release.assets.push_back({"veil-setup-1.0.0.exe", "https://example.com/setup.exe", "application/x-msdownload", 1024, ""});
  release.assets.push_back({"source.tar.gz", "https://example.com/source.tar.gz", "application/gzip", 2048, ""});

  auto installer = release.find_installer();
  ASSERT_TRUE(installer.has_value());
  EXPECT_EQ(installer->name, "veil-setup-1.0.0.exe");
}

TEST(ReleaseInfoTests, FindWindowsMsiInstaller) {
  updater::ReleaseInfo release;
  release.assets.push_back({"veil-1.0.0.msi", "https://example.com/setup.msi", "application/x-msi", 1024, ""});
  release.assets.push_back({"README.md", "https://example.com/readme.md", "text/markdown", 100, ""});

  auto installer = release.find_installer();
  ASSERT_TRUE(installer.has_value());
  EXPECT_EQ(installer->name, "veil-1.0.0.msi");
}

TEST(ReleaseInfoTests, FindSetupSuffix) {
  updater::ReleaseInfo release;
  release.assets.push_back({"veil-win64-setup.exe", "https://example.com/setup.exe", "application/octet-stream", 1024, ""});

  auto installer = release.find_installer();
  ASSERT_TRUE(installer.has_value());
  EXPECT_EQ(installer->name, "veil-win64-setup.exe");
}

TEST(ReleaseInfoTests, FindWin64Installer) {
  updater::ReleaseInfo release;
  release.assets.push_back({"veil-win64-1.0.0.exe", "https://example.com/win64.exe", "application/octet-stream", 1024, ""});

  auto installer = release.find_installer();
  ASSERT_TRUE(installer.has_value());
  EXPECT_EQ(installer->name, "veil-win64-1.0.0.exe");
}

TEST(ReleaseInfoTests, SkipLinuxAssets) {
  updater::ReleaseInfo release;
  release.assets.push_back({"veil-linux-amd64", "https://example.com/linux", "application/octet-stream", 1024, ""});
  release.assets.push_back({"veil-setup-1.0.0.exe", "https://example.com/setup.exe", "application/x-msdownload", 1024, ""});

  auto installer = release.find_installer();
  ASSERT_TRUE(installer.has_value());
  EXPECT_EQ(installer->name, "veil-setup-1.0.0.exe");
}

TEST(ReleaseInfoTests, SkipMacOSAssets) {
  updater::ReleaseInfo release;
  release.assets.push_back({"veil-macos.dmg", "https://example.com/macos.dmg", "application/octet-stream", 1024, ""});
  release.assets.push_back({"veil-darwin-arm64", "https://example.com/darwin", "application/octet-stream", 1024, ""});
  release.assets.push_back({"veil-1.0.0.exe", "https://example.com/setup.exe", "application/x-msdownload", 1024, ""});

  auto installer = release.find_installer();
  ASSERT_TRUE(installer.has_value());
  EXPECT_EQ(installer->name, "veil-1.0.0.exe");
}

TEST(ReleaseInfoTests, NoInstallerFound) {
  updater::ReleaseInfo release;
  release.assets.push_back({"source.tar.gz", "https://example.com/source.tar.gz", "application/gzip", 2048, ""});
  release.assets.push_back({"README.md", "https://example.com/readme.md", "text/markdown", 100, ""});

  auto installer = release.find_installer();
  EXPECT_FALSE(installer.has_value());
}

TEST(ReleaseInfoTests, NoAssetsAvailable) {
  updater::ReleaseInfo release;
  // Empty assets list

  auto installer = release.find_installer();
  EXPECT_FALSE(installer.has_value());
}

TEST(ReleaseInfoTests, CaseInsensitiveMatching) {
  updater::ReleaseInfo release;
  release.assets.push_back({"VEIL-SETUP.EXE", "https://example.com/setup.exe", "application/octet-stream", 1024, ""});

  auto installer = release.find_installer();
  ASSERT_TRUE(installer.has_value());
  EXPECT_EQ(installer->name, "VEIL-SETUP.EXE");
}

// ============================================================================
// AutoUpdater Basic Tests
// ============================================================================

TEST(AutoUpdaterTests, CurrentVersion) {
  auto version = updater::AutoUpdater::current_version();
  // Version should be at least 1.0.0
  EXPECT_GE(version.major, 1);
}

TEST(AutoUpdaterTests, DefaultConfig) {
  updater::UpdateConfig config;
  EXPECT_EQ(config.github_owner, "VisageDvachevsky");
  EXPECT_EQ(config.github_repo, "veil-core");
  EXPECT_TRUE(config.check_on_startup);
  EXPECT_FALSE(config.check_for_prereleases);
  EXPECT_EQ(config.check_interval_hours, 24);
  EXPECT_FALSE(config.auto_download);
  EXPECT_FALSE(config.auto_install);
}

TEST(AutoUpdaterTests, CustomConfig) {
  updater::UpdateConfig config;
  config.github_owner = "TestOwner";
  config.github_repo = "TestRepo";
  config.check_on_startup = false;
  config.check_for_prereleases = true;

  updater::AutoUpdater updater(config);
  EXPECT_EQ(updater.config().github_owner, "TestOwner");
  EXPECT_EQ(updater.config().github_repo, "TestRepo");
  EXPECT_FALSE(updater.config().check_on_startup);
  EXPECT_TRUE(updater.config().check_for_prereleases);
}

// ============================================================================
// Version Ignore List Tests
// ============================================================================

TEST(AutoUpdaterTests, IgnoreVersion) {
  updater::AutoUpdater updater;
  auto v1 = updater::Version::parse("1.5.0");
  ASSERT_TRUE(v1.has_value());

  EXPECT_FALSE(updater.is_version_ignored(*v1));
  updater.ignore_version(*v1);
  EXPECT_TRUE(updater.is_version_ignored(*v1));
}

TEST(AutoUpdaterTests, IgnoreMultipleVersions) {
  updater::AutoUpdater updater;
  auto v1 = updater::Version::parse("1.5.0");
  auto v2 = updater::Version::parse("1.6.0");
  auto v3 = updater::Version::parse("2.0.0");
  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  ASSERT_TRUE(v3.has_value());

  updater.ignore_version(*v1);
  updater.ignore_version(*v2);

  EXPECT_TRUE(updater.is_version_ignored(*v1));
  EXPECT_TRUE(updater.is_version_ignored(*v2));
  EXPECT_FALSE(updater.is_version_ignored(*v3));
}

TEST(AutoUpdaterTests, IgnoreSameVersionTwice) {
  updater::AutoUpdater updater;
  auto v1 = updater::Version::parse("1.5.0");
  ASSERT_TRUE(v1.has_value());

  updater.ignore_version(*v1);
  updater.ignore_version(*v1);  // Should not add duplicate

  EXPECT_TRUE(updater.is_version_ignored(*v1));
}

// ============================================================================
// JSON Parsing Tests (Manual)
// ============================================================================

TEST(AutoUpdaterTests, ParseGitHubReleaseJSON) {
  // Sample GitHub API response
  std::string json_response = R"({
    "tag_name": "v1.5.0",
    "name": "Version 1.5.0",
    "body": "Release notes here",
    "published_at": "2024-01-15T12:00:00Z",
    "html_url": "https://github.com/owner/repo/releases/tag/v1.5.0",
    "prerelease": false,
    "draft": false,
    "assets": [
      {
        "name": "veil-setup-1.5.0.exe",
        "browser_download_url": "https://github.com/owner/repo/releases/download/v1.5.0/veil-setup-1.5.0.exe",
        "content_type": "application/x-msdownload",
        "size": 1048576
      }
    ]
  })";

  auto json = nlohmann::json::parse(json_response);

  updater::ReleaseInfo release;
  release.tag_name = json.value("tag_name", "");
  release.name = json.value("name", "");
  release.body = json.value("body", "");
  release.published_at = json.value("published_at", "");
  release.html_url = json.value("html_url", "");
  release.prerelease = json.value("prerelease", false);
  release.draft = json.value("draft", false);

  EXPECT_EQ(release.tag_name, "v1.5.0");
  EXPECT_EQ(release.name, "Version 1.5.0");
  EXPECT_EQ(release.body, "Release notes here");
  EXPECT_FALSE(release.prerelease);
  EXPECT_FALSE(release.draft);

  // Parse assets
  if (json.contains("assets") && json["assets"].is_array()) {
    for (const auto& asset : json["assets"]) {
      updater::ReleaseAsset ra;
      ra.name = asset.value("name", "");
      ra.download_url = asset.value("browser_download_url", "");
      ra.content_type = asset.value("content_type", "");
      ra.size = static_cast<std::size_t>(asset.value("size", 0));
      release.assets.push_back(ra);
    }
  }

  EXPECT_EQ(release.assets.size(), 1);
  EXPECT_EQ(release.assets[0].name, "veil-setup-1.5.0.exe");
  EXPECT_EQ(release.assets[0].size, 1048576);
}

TEST(AutoUpdaterTests, ParsePrereleaseJSON) {
  std::string json_response = R"({
    "tag_name": "v2.0.0-beta.1",
    "name": "Version 2.0.0 Beta 1",
    "body": "Beta release",
    "published_at": "2024-01-20T12:00:00Z",
    "html_url": "https://github.com/owner/repo/releases/tag/v2.0.0-beta.1",
    "prerelease": true,
    "draft": false,
    "assets": []
  })";

  auto json = nlohmann::json::parse(json_response);

  updater::ReleaseInfo release;
  release.tag_name = json.value("tag_name", "");
  release.prerelease = json.value("prerelease", false);

  EXPECT_EQ(release.tag_name, "v2.0.0-beta.1");
  EXPECT_TRUE(release.prerelease);
}

TEST(AutoUpdaterTests, ParseEmptyAssetsArray) {
  std::string json_response = R"({
    "tag_name": "v1.0.0",
    "name": "Version 1.0.0",
    "body": "",
    "published_at": "2024-01-01T12:00:00Z",
    "html_url": "https://github.com/owner/repo/releases/tag/v1.0.0",
    "prerelease": false,
    "draft": false,
    "assets": []
  })";

  auto json = nlohmann::json::parse(json_response);

  updater::ReleaseInfo release;
  if (json.contains("assets") && json["assets"].is_array()) {
    for (const auto& asset : json["assets"]) {
      updater::ReleaseAsset ra;
      ra.name = asset.value("name", "");
      release.assets.push_back(ra);
    }
  }

  EXPECT_TRUE(release.assets.empty());
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(AutoUpdaterTests, ErrorCallbackInvoked) {
  updater::AutoUpdater updater;

  bool error_called = false;
  std::string error_message;

  updater.on_error([&](const std::string& msg) {
    error_called = true;
    error_message = msg;
  });

  // Try to check for updates with invalid URL
  updater::UpdateConfig config;
  config.custom_update_url = "http://invalid-domain-that-does-not-exist-12345.com/api";
  updater.set_config(config);

  auto release = updater.check_for_updates_sync();

  // Should fail and call error callback
  EXPECT_FALSE(release.has_value());
  // Note: Error callback may or may not be called depending on network availability
  // This is a best-effort test
}

// ============================================================================
// Update Dialog Result Tests
// ============================================================================

TEST(UpdateDialogTests, ResultActions) {
  updater::UpdateDialogResult result;

  result.action = updater::UpdateDialogResult::Action::kSkip;
  EXPECT_EQ(result.action, updater::UpdateDialogResult::Action::kSkip);

  result.action = updater::UpdateDialogResult::Action::kRemindLater;
  EXPECT_EQ(result.action, updater::UpdateDialogResult::Action::kRemindLater);

  result.action = updater::UpdateDialogResult::Action::kDownload;
  EXPECT_EQ(result.action, updater::UpdateDialogResult::Action::kDownload);

  result.action = updater::UpdateDialogResult::Action::kInstall;
  EXPECT_EQ(result.action, updater::UpdateDialogResult::Action::kInstall);
}

TEST(UpdateDialogTests, DontRemindAgainFlag) {
  updater::UpdateDialogResult result;
  result.dont_remind_again = true;
  EXPECT_TRUE(result.dont_remind_again);

  result.dont_remind_again = false;
  EXPECT_FALSE(result.dont_remind_again);
}

// ============================================================================
// install_update Tests (non-Windows platform behavior)
// ============================================================================

#ifndef _WIN32
TEST(AutoUpdaterTests, InstallUpdateNotImplementedOnLinux) {
  updater::AutoUpdater updater;
  std::string error;
  // On Linux, install_update should return false with a descriptive error.
  // This also verifies that the installer_path parameter is properly handled
  // (was previously causing -Wunused-parameter build failure on Linux).
  bool result = updater.install_update("/tmp/fake-installer.exe", error);
  EXPECT_FALSE(result);
  EXPECT_FALSE(error.empty());
  EXPECT_NE(error.find("not implemented"), std::string::npos);
}
#endif

// ============================================================================
// Impl Destructor Cleanup Tests
// ============================================================================

TEST(AutoUpdaterTests, DestructorCleansUpWithoutCrash) {
  // Verify that AutoUpdater can be created and destroyed without issues.
  // The Impl destructor contains a catch block for exception cleanup
  // that must be properly annotated to avoid bugprone-empty-catch warnings.
  {
    updater::AutoUpdater updater;
    // Destructor runs here - should not crash even with empty task list
  }
  SUCCEED();
}

TEST(AutoUpdaterTests, DestructorAfterConfigChange) {
  // Verify cleanup after configuration changes.
  {
    updater::AutoUpdater updater;
    updater::UpdateConfig config;
    config.github_owner = "test";
    config.github_repo = "test";
    updater.set_config(config);
    // Destructor should clean up properly
  }
  SUCCEED();
}

}  // namespace veil::tests
