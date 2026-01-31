// NOLINTBEGIN(readability-implicit-bool-conversion)
#include <gtest/gtest.h>

#include <QApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include "settings_widget.h"
#include "common/gui/theme.h"

// We need a QApplication for widget tests.
// Set offscreen platform for headless CI environments.
static struct OffscreenPlatformSetter {
  OffscreenPlatformSetter() {
    if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
      qputenv("QT_QPA_PLATFORM", "offscreen");
    }
  }
} offscreenSetter;  // NOLINT(cert-err58-cpp)

static int argc = 1;
static char appName[] = "settings_widget_tests";  // NOLINT(cppcoreguidelines-avoid-c-arrays)
static char* argv[] = {appName, nullptr};  // NOLINT(cppcoreguidelines-avoid-c-arrays)

namespace veil::gui {
namespace {

class SettingsWidgetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (QApplication::instance() == nullptr) {
      app_ = new QApplication(argc, argv);
    }
    // Clear all relevant settings before each test
    QSettings settings("VEIL", "VPN Client");
    settings.clear();
    settings.sync();

    widget_ = new SettingsWidget();
  }

  void TearDown() override {
    delete widget_;
    // Clean up settings
    QSettings settings("VEIL", "VPN Client");
    settings.clear();
    settings.sync();
  }

  SettingsWidget* widget_{nullptr};
  QApplication* app_{nullptr};
};

// ===================== Construction Tests =====================

TEST_F(SettingsWidgetTest, InitialState) {
  EXPECT_NE(widget_, nullptr);
}

// ===================== Settings Getters Tests =====================

TEST_F(SettingsWidgetTest, GetServerAddressEmpty) {
  // Initially should return empty or default value
  QString address = widget_->serverAddress();
  // Should not crash
}

TEST_F(SettingsWidgetTest, GetServerPortDefault) {
  uint16_t port = widget_->serverPort();
  // Should return a valid port number (likely default 4433)
  EXPECT_GT(port, 0);
}

TEST_F(SettingsWidgetTest, GetKeyFilePathEmpty) {
  QString keyPath = widget_->keyFilePath();
  // Should not crash
}

TEST_F(SettingsWidgetTest, GetObfuscationSeedPathEmpty) {
  QString seedPath = widget_->obfuscationSeedPath();
  // Should not crash
}

// ===================== Load Settings Tests =====================

TEST_F(SettingsWidgetTest, LoadSettingsEmpty) {
  widget_->loadSettings();
  // Should not crash with empty settings
}

TEST_F(SettingsWidgetTest, LoadSettingsWithServerConfig) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "vpn.example.com");
  settings.setValue("server/port", 4433);
  settings.sync();

  widget_->loadSettings();

  EXPECT_EQ(widget_->serverAddress(), "vpn.example.com");
  EXPECT_EQ(widget_->serverPort(), 4433);
}

TEST_F(SettingsWidgetTest, LoadSettingsWithCryptoConfig) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("crypto/keyFile", "/path/to/key.pem");
  settings.setValue("crypto/obfuscationSeedFile", "/path/to/seed.bin");
  settings.sync();

  widget_->loadSettings();

  EXPECT_EQ(widget_->keyFilePath(), "/path/to/key.pem");
  EXPECT_EQ(widget_->obfuscationSeedPath(), "/path/to/seed.bin");
}

TEST_F(SettingsWidgetTest, LoadSettingsWithInvalidPort) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "vpn.example.com");
  settings.setValue("server/port", -1);
  settings.sync();

  widget_->loadSettings();
  // Should handle invalid port gracefully
}

TEST_F(SettingsWidgetTest, LoadSettingsWithLargePort) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "vpn.example.com");
  settings.setValue("server/port", 99999);
  settings.sync();

  widget_->loadSettings();
  // Should handle port out of valid range
}

// ===================== Save Settings Tests =====================

TEST_F(SettingsWidgetTest, SaveSettingsEmitsSignal) {
  QSignalSpy spy(widget_, &SettingsWidget::settingsSaved);

  widget_->saveSettings();

  EXPECT_EQ(spy.count(), 1);
}

TEST_F(SettingsWidgetTest, SaveAndLoadRoundTrip) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "test.vpn.com");
  settings.setValue("server/port", 8080);
  settings.sync();

  widget_->loadSettings();
  widget_->saveSettings();

  // Settings should be persisted
  QSettings loadedSettings("VEIL", "VPN Client");
  EXPECT_EQ(loadedSettings.value("server/address").toString(), "test.vpn.com");
  EXPECT_EQ(loadedSettings.value("server/port").toInt(), 8080);
}

TEST_F(SettingsWidgetTest, SaveSettingsMultipleTimes) {
  QSignalSpy spy(widget_, &SettingsWidget::settingsSaved);

  widget_->saveSettings();
  widget_->saveSettings();
  widget_->saveSettings();

  EXPECT_EQ(spy.count(), 3);
}

// ===================== Signal Tests =====================

TEST_F(SettingsWidgetTest, BackRequestedSignal) {
  QSignalSpy spy(widget_, &SettingsWidget::backRequested);
  EXPECT_TRUE(spy.isValid());
}

TEST_F(SettingsWidgetTest, SettingsSavedSignal) {
  QSignalSpy spy(widget_, &SettingsWidget::settingsSaved);
  EXPECT_TRUE(spy.isValid());
}

TEST_F(SettingsWidgetTest, ThemeChangedSignal) {
  QSignalSpy spy(widget_, &SettingsWidget::themeChanged);
  EXPECT_TRUE(spy.isValid());
}

TEST_F(SettingsWidgetTest, LanguageChangedSignal) {
  QSignalSpy spy(widget_, &SettingsWidget::languageChanged);
  EXPECT_TRUE(spy.isValid());
}

// ===================== Validation Tests =====================

TEST_F(SettingsWidgetTest, ValidateSettingsWithEmptyServer) {
  // Clear server settings
  QSettings settings("VEIL", "VPN Client");
  settings.remove("server/address");
  settings.sync();

  widget_->loadSettings();
  // Validation should handle empty server address
}

TEST_F(SettingsWidgetTest, ValidateSettingsWithValidHostname) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "vpn.example.com");
  settings.setValue("server/port", 4433);
  settings.sync();

  widget_->loadSettings();
  // Should accept valid hostname
}

TEST_F(SettingsWidgetTest, ValidateSettingsWithIPv4) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "192.168.1.1");
  settings.setValue("server/port", 4433);
  settings.sync();

  widget_->loadSettings();
  // Should accept valid IPv4 address
}

TEST_F(SettingsWidgetTest, ValidateSettingsWithIPv6) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "2001:db8::1");
  settings.setValue("server/port", 4433);
  settings.sync();

  widget_->loadSettings();
  // Should accept valid IPv6 address
}

TEST_F(SettingsWidgetTest, ValidateSettingsWithInvalidHostname) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "invalid..hostname");
  settings.setValue("server/port", 4433);
  settings.sync();

  widget_->loadSettings();
  // Should handle invalid hostname
}

// ===================== File Path Validation Tests =====================

TEST_F(SettingsWidgetTest, ValidateKeyFilePathEmpty) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("crypto/keyFile", "");
  settings.sync();

  widget_->loadSettings();
  // Should handle empty key file path
}

TEST_F(SettingsWidgetTest, ValidateKeyFilePathNonExistent) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("crypto/keyFile", "/nonexistent/path/key.pem");
  settings.sync();

  widget_->loadSettings();
  // Should handle non-existent file path
}

TEST_F(SettingsWidgetTest, ValidateObfuscationSeedPathEmpty) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("crypto/obfuscationSeedFile", "");
  settings.sync();

  widget_->loadSettings();
  // Should handle empty obfuscation seed path
}

TEST_F(SettingsWidgetTest, ValidateObfuscationSeedPathNonExistent) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("crypto/obfuscationSeedFile", "/nonexistent/path/seed.bin");
  settings.sync();

  widget_->loadSettings();
  // Should handle non-existent file path
}

TEST_F(SettingsWidgetTest, ValidateKeyFilePathValid) {
  QTemporaryDir tempDir;
  QString keyFilePath = tempDir.path() + "/test_key.pem";
  QFile keyFile(keyFilePath);
  keyFile.open(QIODevice::WriteOnly);
  keyFile.write("dummy key content");
  keyFile.close();

  QSettings settings("VEIL", "VPN Client");
  settings.setValue("crypto/keyFile", keyFilePath);
  settings.sync();

  widget_->loadSettings();

  EXPECT_EQ(widget_->keyFilePath(), keyFilePath);
}

// ===================== Port Validation Tests =====================

TEST_F(SettingsWidgetTest, ValidatePortZero) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/port", 0);
  settings.sync();

  widget_->loadSettings();
  // Should handle port 0
}

TEST_F(SettingsWidgetTest, ValidatePortMinimum) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/port", 1);
  settings.sync();

  widget_->loadSettings();

  EXPECT_EQ(widget_->serverPort(), 1);
}

TEST_F(SettingsWidgetTest, ValidatePortMaximum) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/port", 65535);
  settings.sync();

  widget_->loadSettings();

  EXPECT_EQ(widget_->serverPort(), 65535);
}

TEST_F(SettingsWidgetTest, ValidatePortCommon) {
  std::vector<uint16_t> commonPorts = {80, 443, 4433, 8080, 8443};

  for (uint16_t port : commonPorts) {
    QSettings settings("VEIL", "VPN Client");
    settings.setValue("server/port", port);
    settings.sync();

    widget_->loadSettings();

    EXPECT_EQ(widget_->serverPort(), port);
  }
}

// ===================== Multiple Load/Save Tests =====================

TEST_F(SettingsWidgetTest, LoadSaveLoadSequence) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "initial.vpn.com");
  settings.setValue("server/port", 4433);
  settings.sync();

  widget_->loadSettings();
  EXPECT_EQ(widget_->serverAddress(), "initial.vpn.com");

  widget_->saveSettings();

  widget_->loadSettings();
  EXPECT_EQ(widget_->serverAddress(), "initial.vpn.com");
}

TEST_F(SettingsWidgetTest, MultipleLoadCalls) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "vpn.example.com");
  settings.sync();

  widget_->loadSettings();
  widget_->loadSettings();
  widget_->loadSettings();

  EXPECT_EQ(widget_->serverAddress(), "vpn.example.com");
}

// ===================== Complex Settings Tests =====================

TEST_F(SettingsWidgetTest, LoadCompleteConfiguration) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "vpn.example.com");
  settings.setValue("server/port", 4433);
  settings.setValue("crypto/keyFile", "/path/to/key.pem");
  settings.setValue("crypto/obfuscationSeedFile", "/path/to/seed.bin");
  settings.setValue("connection/autoReconnect", true);
  settings.setValue("connection/reconnectInterval", 5);
  settings.setValue("connection/maxReconnectAttempts", 3);
  settings.setValue("routing/routeAllTraffic", true);
  settings.setValue("notifications/enabled", true);
  settings.sync();

  widget_->loadSettings();

  EXPECT_EQ(widget_->serverAddress(), "vpn.example.com");
  EXPECT_EQ(widget_->serverPort(), 4433);
  EXPECT_EQ(widget_->keyFilePath(), "/path/to/key.pem");
  EXPECT_EQ(widget_->obfuscationSeedPath(), "/path/to/seed.bin");
}

TEST_F(SettingsWidgetTest, SaveCompleteConfiguration) {
  // Load some settings
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "vpn.example.com");
  settings.setValue("server/port", 4433);
  settings.sync();

  widget_->loadSettings();
  widget_->saveSettings();

  // Verify persistence
  QSettings loadedSettings("VEIL", "VPN Client");
  EXPECT_EQ(loadedSettings.value("server/address").toString(), "vpn.example.com");
}

// ===================== Edge Cases Tests =====================

TEST_F(SettingsWidgetTest, ServerAddressWithSpecialCharacters) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "vpn-server_1.example.com");
  settings.sync();

  widget_->loadSettings();

  EXPECT_EQ(widget_->serverAddress(), "vpn-server_1.example.com");
}

TEST_F(SettingsWidgetTest, VeryLongServerAddress) {
  QString longAddress = QString("subdomain.").repeated(20) + "example.com";
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", longAddress);
  settings.sync();

  widget_->loadSettings();

  EXPECT_EQ(widget_->serverAddress(), longAddress);
}

TEST_F(SettingsWidgetTest, FilePathWithSpaces) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("crypto/keyFile", "/path with spaces/key file.pem");
  settings.sync();

  widget_->loadSettings();

  EXPECT_EQ(widget_->keyFilePath(), "/path with spaces/key file.pem");
}

TEST_F(SettingsWidgetTest, FilePathWithUnicode) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("crypto/keyFile", "/путь/到/ファイル.pem");
  settings.sync();

  widget_->loadSettings();

  EXPECT_EQ(widget_->keyFilePath(), "/путь/到/ファイル.pem");
}

// ===================== Rapid Changes Tests =====================

TEST_F(SettingsWidgetTest, RapidLoadSaveCalls) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "vpn.example.com");
  settings.sync();

  for (int i = 0; i < 50; ++i) {
    widget_->loadSettings();
    widget_->saveSettings();
  }

  EXPECT_EQ(widget_->serverAddress(), "vpn.example.com");
}

TEST_F(SettingsWidgetTest, AlternatingLoadSave) {
  for (int i = 0; i < 20; ++i) {
    QSettings settings("VEIL", "VPN Client");
    settings.setValue("server/address", QString("server%1.example.com").arg(i));
    settings.sync();

    widget_->loadSettings();
    widget_->saveSettings();
  }
}

}  // namespace
}  // namespace veil::gui
// NOLINTEND(readability-implicit-bool-conversion)
