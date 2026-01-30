// NOLINTBEGIN(readability-implicit-bool-conversion)
#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QSettings>
#include "quick_actions_widget.h"
#include "connection_widget.h"

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
static char appName[] = "quick_actions_widget_tests";  // NOLINT(cppcoreguidelines-avoid-c-arrays)
static char* argv[] = {appName, nullptr};  // NOLINT(cppcoreguidelines-avoid-c-arrays)

namespace veil::gui {
namespace {

class QuickActionsWidgetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (QApplication::instance() == nullptr) {
      app_ = new QApplication(argc, argv);
    }
    // Clear relevant settings before each test
    QSettings settings("VEIL", "VPN Client");
    settings.remove("quickActions");
    settings.sync();

    widget_ = new QuickActionsWidget();
  }

  void TearDown() override {
    delete widget_;
    // Clean up settings
    QSettings settings("VEIL", "VPN Client");
    settings.remove("quickActions");
    settings.sync();
  }

  QuickActionsWidget* widget_{nullptr};
  QApplication* app_{nullptr};
};

// ===================== Construction Tests =====================

TEST_F(QuickActionsWidgetTest, InitialState) {
  EXPECT_NE(widget_, nullptr);
  // Kill switch should default to off
  EXPECT_FALSE(widget_->isKillSwitchEnabled());
}

TEST_F(QuickActionsWidgetTest, DefaultObfuscationState) {
  // Obfuscation defaults to enabled
  EXPECT_TRUE(widget_->isObfuscationEnabled());
}

// ===================== Kill Switch Tests =====================

TEST_F(QuickActionsWidgetTest, KillSwitchToggle) {
  EXPECT_FALSE(widget_->isKillSwitchEnabled());

  widget_->setKillSwitchEnabled(true);
  EXPECT_TRUE(widget_->isKillSwitchEnabled());

  widget_->setKillSwitchEnabled(false);
  EXPECT_FALSE(widget_->isKillSwitchEnabled());
}

TEST_F(QuickActionsWidgetTest, KillSwitchSetAndGet) {
  // Test the setter and getter directly
  widget_->setKillSwitchEnabled(true);
  EXPECT_TRUE(widget_->isKillSwitchEnabled());

  widget_->setKillSwitchEnabled(false);
  EXPECT_FALSE(widget_->isKillSwitchEnabled());
}

TEST_F(QuickActionsWidgetTest, KillSwitchPersistence) {
  widget_->setKillSwitchEnabled(true);

  QSettings settings("VEIL", "VPN Client");
  EXPECT_TRUE(settings.value("quickActions/killSwitch", false).toBool());
}

// ===================== Obfuscation Tests =====================

TEST_F(QuickActionsWidgetTest, ObfuscationToggle) {
  EXPECT_TRUE(widget_->isObfuscationEnabled());

  widget_->setObfuscationEnabled(false);
  EXPECT_FALSE(widget_->isObfuscationEnabled());

  widget_->setObfuscationEnabled(true);
  EXPECT_TRUE(widget_->isObfuscationEnabled());
}

TEST_F(QuickActionsWidgetTest, ObfuscationPersistence) {
  widget_->setObfuscationEnabled(false);

  QSettings settings("VEIL", "VPN Client");
  EXPECT_FALSE(settings.value("advanced/obfuscation", true).toBool());
}

// ===================== IP Address Tests =====================

TEST_F(QuickActionsWidgetTest, SetIpAddress) {
  // Should not crash
  widget_->setIpAddress("10.0.0.1", 4433);
}

TEST_F(QuickActionsWidgetTest, SetEmptyIpAddress) {
  // Should not crash
  widget_->setIpAddress("", 0);
}

// ===================== Connection State Tests =====================

TEST_F(QuickActionsWidgetTest, SetConnectionStateDisconnected) {
  widget_->setConnectionState(ConnectionState::kDisconnected);
  // Should not crash
}

TEST_F(QuickActionsWidgetTest, SetConnectionStateConnected) {
  widget_->setConnectionState(ConnectionState::kConnected);
  // Should not crash
}

TEST_F(QuickActionsWidgetTest, SetConnectionStateConnecting) {
  widget_->setConnectionState(ConnectionState::kConnecting);
  // Should not crash
}

TEST_F(QuickActionsWidgetTest, SetConnectionStateError) {
  widget_->setConnectionState(ConnectionState::kError);
  // Should not crash
}

TEST_F(QuickActionsWidgetTest, SetConnectionStateReconnecting) {
  widget_->setConnectionState(ConnectionState::kReconnecting);
  // Should not crash
}

// ===================== Combined State Tests =====================

TEST_F(QuickActionsWidgetTest, FullConnectionLifecycle) {
  // Simulate a full connection lifecycle
  widget_->setConnectionState(ConnectionState::kDisconnected);
  widget_->setIpAddress("vpn.example.com", 4433);

  widget_->setConnectionState(ConnectionState::kConnecting);
  widget_->setConnectionState(ConnectionState::kConnected);

  widget_->setKillSwitchEnabled(true);
  EXPECT_TRUE(widget_->isKillSwitchEnabled());

  widget_->setConnectionState(ConnectionState::kDisconnected);
}

TEST_F(QuickActionsWidgetTest, MultipleStateChanges) {
  // Rapidly changing states should not crash
  for (int i = 0; i < 10; ++i) {
    widget_->setConnectionState(ConnectionState::kConnecting);
    widget_->setConnectionState(ConnectionState::kConnected);
    widget_->setConnectionState(ConnectionState::kDisconnected);
  }
}

TEST_F(QuickActionsWidgetTest, TogglesDuringConnection) {
  widget_->setConnectionState(ConnectionState::kConnected);
  widget_->setIpAddress("10.0.0.1", 4433);

  // Toggle features while connected
  widget_->setKillSwitchEnabled(true);
  widget_->setObfuscationEnabled(false);

  EXPECT_TRUE(widget_->isKillSwitchEnabled());
  EXPECT_FALSE(widget_->isObfuscationEnabled());

  // Reverse toggles
  widget_->setKillSwitchEnabled(false);
  widget_->setObfuscationEnabled(true);

  EXPECT_FALSE(widget_->isKillSwitchEnabled());
  EXPECT_TRUE(widget_->isObfuscationEnabled());
}

}  // namespace
}  // namespace veil::gui

// NOLINTEND(readability-implicit-bool-conversion)
