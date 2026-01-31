#include <gtest/gtest.h>

#ifdef _WIN32
#include "../../src/windows/shortcut_manager.h"
#include <filesystem>
#include <windows.h>
#include <shlobj.h>

using namespace veil::windows;

namespace {

// Helper to get a temporary test location
std::string getTempTestDir() {
  char temp_path[MAX_PATH];
  GetTempPathA(MAX_PATH, temp_path);
  std::filesystem::path test_dir = std::filesystem::path(temp_path) / "veil_shortcut_tests";
  std::filesystem::create_directories(test_dir);
  return test_dir.string();
}

// Helper to clean up test shortcuts
void cleanupTestShortcut(const std::string& dir, const std::string& name) {
  std::filesystem::path shortcut_path = std::filesystem::path(dir) / (name + ".lnk");
  std::error_code ec;
  std::filesystem::remove(shortcut_path, ec);
}

}  // namespace

class ShortcutManagerTests : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = getTempTestDir();
    test_shortcut_name_ = "VeilTestShortcut";
  }

  void TearDown() override {
    // Clean up test shortcuts
    cleanupTestShortcut(test_dir_, test_shortcut_name_);

    // Try to remove test directory (may fail if not empty)
    std::error_code ec;
    std::filesystem::remove(test_dir_, ec);
  }

  std::string test_dir_;
  std::string test_shortcut_name_;
};

// Test getting Desktop location path
TEST_F(ShortcutManagerTests, GetDesktopLocationPath) {
  std::string error;
  std::string desktop_path = ShortcutManager::getLocationPath(
      ShortcutManager::Location::kDesktop, error);

  EXPECT_FALSE(desktop_path.empty()) << "Desktop path should not be empty. Error: " << error;
  EXPECT_TRUE(error.empty()) << "Should not have error: " << error;
  EXPECT_TRUE(std::filesystem::exists(desktop_path)) << "Desktop path should exist: " << desktop_path;
}

// Test getting Start Menu location path
TEST_F(ShortcutManagerTests, GetStartMenuLocationPath) {
  std::string error;
  std::string start_menu_path = ShortcutManager::getLocationPath(
      ShortcutManager::Location::kStartMenu, error);

  EXPECT_FALSE(start_menu_path.empty()) << "Start Menu path should not be empty. Error: " << error;
  EXPECT_TRUE(error.empty()) << "Should not have error: " << error;
  EXPECT_TRUE(std::filesystem::exists(start_menu_path)) << "Start Menu path should exist: " << start_menu_path;
}

// Test creating a basic shortcut to notepad.exe
TEST_F(ShortcutManagerTests, CreateBasicShortcut) {
  // Use notepad.exe as a test target (always exists on Windows)
  std::string target_path = "C:\\Windows\\System32\\notepad.exe";
  ASSERT_TRUE(std::filesystem::exists(target_path)) << "Test target should exist";

  // Create a temporary directory for testing
  std::string test_location = test_dir_;

  std::string error;
  bool success = ShortcutManager::createShortcut(
      ShortcutManager::Location::kDesktop,  // We'll override the path below
      test_shortcut_name_,
      target_path,
      error,
      "",  // arguments
      "Test shortcut for VEIL unit tests",
      "",  // icon_path
      0,   // icon_index
      ""   // working_dir
  );

  // Note: This test will create the shortcut on the actual Desktop
  // In a real test environment, you'd want to mock this or use a temp location
  if (success) {
    // Verify the shortcut was created
    std::string desktop_error;
    std::string desktop_path = ShortcutManager::getLocationPath(
        ShortcutManager::Location::kDesktop, desktop_error);
    std::filesystem::path shortcut_path = std::filesystem::path(desktop_path) / (test_shortcut_name_ + ".lnk");

    EXPECT_TRUE(std::filesystem::exists(shortcut_path))
        << "Shortcut should exist at: " << shortcut_path.string();

    // Clean up the test shortcut from Desktop
    std::string cleanup_error;
    ShortcutManager::removeShortcut(
        ShortcutManager::Location::kDesktop,
        test_shortcut_name_,
        cleanup_error);
  } else {
    // Log the error but don't fail the test if we can't create shortcuts
    // (might happen in restricted environments)
    ADD_FAILURE() << "Failed to create shortcut: " << error;
  }
}

// Test checking if a shortcut exists
TEST_F(ShortcutManagerTests, ShortcutExists) {
  // First, create a shortcut
  std::string target_path = "C:\\Windows\\System32\\notepad.exe";
  std::string error;

  bool created = ShortcutManager::createShortcut(
      ShortcutManager::Location::kDesktop,
      test_shortcut_name_,
      target_path,
      error,
      "",
      "Test shortcut",
      "",
      0,
      ""
  );

  if (created) {
    // Check that it exists
    bool exists = ShortcutManager::shortcutExists(
        ShortcutManager::Location::kDesktop,
        test_shortcut_name_
    );
    EXPECT_TRUE(exists) << "Shortcut should exist after creation";

    // Clean up
    ShortcutManager::removeShortcut(
        ShortcutManager::Location::kDesktop,
        test_shortcut_name_,
        error
    );

    // Check that it no longer exists
    exists = ShortcutManager::shortcutExists(
        ShortcutManager::Location::kDesktop,
        test_shortcut_name_
    );
    EXPECT_FALSE(exists) << "Shortcut should not exist after removal";
  } else {
    GTEST_SKIP() << "Cannot create shortcuts in test environment: " << error;
  }
}

// Test removing a shortcut
TEST_F(ShortcutManagerTests, RemoveShortcut) {
  std::string target_path = "C:\\Windows\\System32\\notepad.exe";
  std::string error;

  // Create shortcut
  bool created = ShortcutManager::createShortcut(
      ShortcutManager::Location::kDesktop,
      test_shortcut_name_,
      target_path,
      error,
      "",
      "Test shortcut",
      "",
      0,
      ""
  );

  if (created) {
    // Remove it
    bool removed = ShortcutManager::removeShortcut(
        ShortcutManager::Location::kDesktop,
        test_shortcut_name_,
        error
    );
    EXPECT_TRUE(removed) << "Should successfully remove shortcut. Error: " << error;

    // Verify it's gone
    bool exists = ShortcutManager::shortcutExists(
        ShortcutManager::Location::kDesktop,
        test_shortcut_name_
    );
    EXPECT_FALSE(exists) << "Shortcut should not exist after removal";
  } else {
    GTEST_SKIP() << "Cannot create shortcuts in test environment: " << error;
  }
}

// Test removing a non-existent shortcut (should succeed)
TEST_F(ShortcutManagerTests, RemoveNonExistentShortcut) {
  std::string error;
  bool removed = ShortcutManager::removeShortcut(
      ShortcutManager::Location::kDesktop,
      "NonExistentShortcut_12345",
      error
  );

  EXPECT_TRUE(removed) << "Removing non-existent shortcut should succeed. Error: " << error;
  EXPECT_TRUE(error.empty()) << "Should not have error when removing non-existent shortcut";
}

// Test creating shortcut with arguments
TEST_F(ShortcutManagerTests, CreateShortcutWithArguments) {
  std::string target_path = "C:\\Windows\\System32\\notepad.exe";
  std::string arguments = "C:\\test.txt";
  std::string error;

  bool created = ShortcutManager::createShortcut(
      ShortcutManager::Location::kDesktop,
      test_shortcut_name_,
      target_path,
      error,
      arguments,
      "Test shortcut with arguments",
      "",
      0,
      ""
  );

  if (created) {
    // Verify creation
    bool exists = ShortcutManager::shortcutExists(
        ShortcutManager::Location::kDesktop,
        test_shortcut_name_
    );
    EXPECT_TRUE(exists) << "Shortcut with arguments should exist";

    // Clean up
    ShortcutManager::removeShortcut(
        ShortcutManager::Location::kDesktop,
        test_shortcut_name_,
        error
    );
  } else {
    GTEST_SKIP() << "Cannot create shortcuts in test environment: " << error;
  }
}

// Test pinToTaskbar (expected to return false as it's not implemented)
TEST_F(ShortcutManagerTests, PinToTaskbar) {
  std::string target_path = "C:\\Windows\\System32\\notepad.exe";
  bool pinned = ShortcutManager::pinToTaskbar(target_path);

  // Currently not implemented, should return false
  EXPECT_FALSE(pinned) << "Pin to taskbar is not implemented and should return false";
}

#else  // Non-Windows platforms

TEST(ShortcutManagerTests, NonWindowsPlatformSkip) {
  GTEST_SKIP() << "ShortcutManager tests only run on Windows";
}

#endif  // _WIN32
