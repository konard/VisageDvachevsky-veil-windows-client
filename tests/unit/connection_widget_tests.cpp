// NOLINTBEGIN(readability-implicit-bool-conversion)
#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include "connection_widget.h"
#include "common/gui/error_message.h"

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
static char appName[] = "connection_widget_tests";  // NOLINT(cppcoreguidelines-avoid-c-arrays)
static char* argv[] = {appName, nullptr};  // NOLINT(cppcoreguidelines-avoid-c-arrays)

namespace veil::gui {
namespace {

class ConnectionWidgetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (QApplication::instance() == nullptr) {
      app_ = new QApplication(argc, argv);
    }
    // Clear relevant settings before each test
    QSettings settings("VEIL", "VPN Client");
    settings.remove("server");
    settings.sync();

    widget_ = new ConnectionWidget();
  }

  void TearDown() override {
    delete widget_;
    // Clean up settings
    QSettings settings("VEIL", "VPN Client");
    settings.remove("server");
    settings.sync();
  }

  ConnectionWidget* widget_{nullptr};
  QApplication* app_{nullptr};
};

// ===================== Construction Tests =====================

TEST_F(ConnectionWidgetTest, InitialState) {
  EXPECT_NE(widget_, nullptr);
  // Widget should start in disconnected state
  // (No public getter for state, but we can verify via UI behavior)
}

// ===================== Connection State Tests =====================

TEST_F(ConnectionWidgetTest, SetConnectionStateDisconnected) {
  widget_->setConnectionState(ConnectionState::kDisconnected);
  // Should not crash
}

TEST_F(ConnectionWidgetTest, SetConnectionStateConnecting) {
  widget_->setConnectionState(ConnectionState::kConnecting);
  // Should not crash and should trigger animations
}

TEST_F(ConnectionWidgetTest, SetConnectionStateConnected) {
  widget_->setConnectionState(ConnectionState::kConnected);
  // Should not crash and update UI to connected state
}

TEST_F(ConnectionWidgetTest, SetConnectionStateReconnecting) {
  widget_->setConnectionState(ConnectionState::kReconnecting);
  // Should not crash
}

TEST_F(ConnectionWidgetTest, SetConnectionStateError) {
  widget_->setConnectionState(ConnectionState::kError);
  // Should not crash and display error UI
}

TEST_F(ConnectionWidgetTest, StateTransitionSequence) {
  // Test typical connection sequence
  widget_->setConnectionState(ConnectionState::kDisconnected);
  widget_->setConnectionState(ConnectionState::kConnecting);
  widget_->setConnectionState(ConnectionState::kConnected);
  widget_->setConnectionState(ConnectionState::kDisconnected);
}

TEST_F(ConnectionWidgetTest, StateTransitionWithReconnect) {
  // Test reconnection sequence
  widget_->setConnectionState(ConnectionState::kConnected);
  widget_->setConnectionState(ConnectionState::kReconnecting);
  widget_->setConnectionState(ConnectionState::kConnected);
}

TEST_F(ConnectionWidgetTest, StateTransitionToError) {
  // Test error handling
  widget_->setConnectionState(ConnectionState::kConnecting);
  widget_->setConnectionState(ConnectionState::kError);
  widget_->setConnectionState(ConnectionState::kDisconnected);
}

// ===================== Metrics Update Tests =====================

TEST_F(ConnectionWidgetTest, UpdateMetricsBasic) {
  widget_->setConnectionState(ConnectionState::kConnected);
  widget_->updateMetrics(50, 1024ULL * 100, 1024ULL * 50);  // 50ms, 100KB/s, 50KB/s
  // Should not crash
}

TEST_F(ConnectionWidgetTest, UpdateMetricsZeroValues) {
  widget_->setConnectionState(ConnectionState::kConnected);
  widget_->updateMetrics(0, 0, 0);
  // Should handle zero values gracefully
}

TEST_F(ConnectionWidgetTest, UpdateMetricsHighLatency) {
  widget_->setConnectionState(ConnectionState::kConnected);
  widget_->updateMetrics(500, 1024, 1024);  // High latency
  // Should not crash
}

TEST_F(ConnectionWidgetTest, UpdateMetricsLargeThroughput) {
  widget_->setConnectionState(ConnectionState::kConnected);
  widget_->updateMetrics(10, 1024ULL * 1024 * 100, 1024ULL * 1024 * 50);  // 100MB/s, 50MB/s
  // Should handle large values
}

TEST_F(ConnectionWidgetTest, UpdateMetricsWhileDisconnected) {
  widget_->setConnectionState(ConnectionState::kDisconnected);
  widget_->updateMetrics(50, 1024, 1024);
  // Should handle metrics update even when disconnected
}

// ===================== Session Info Tests =====================

TEST_F(ConnectionWidgetTest, SetSessionId) {
  widget_->setSessionId("session-12345");
  // Should not crash
}

TEST_F(ConnectionWidgetTest, SetSessionIdEmpty) {
  widget_->setSessionId("");
  // Should handle empty session ID
}

TEST_F(ConnectionWidgetTest, SetSessionIdMultipleTimes) {
  widget_->setSessionId("session-1");
  widget_->setSessionId("session-2");
  widget_->setSessionId("session-3");
  // Should handle multiple updates
}

TEST_F(ConnectionWidgetTest, SetServerAddress) {
  widget_->setServerAddress("vpn.example.com", 4433);
  // Should not crash
}

TEST_F(ConnectionWidgetTest, SetServerAddressIPv4) {
  widget_->setServerAddress("192.168.1.1", 8080);
  // Should handle IPv4 addresses
}

TEST_F(ConnectionWidgetTest, SetServerAddressIPv6) {
  widget_->setServerAddress("2001:db8::1", 443);
  // Should handle IPv6 addresses
}

TEST_F(ConnectionWidgetTest, SetServerAddressDefaultPort) {
  widget_->setServerAddress("vpn.example.com", 0);
  // Should handle port 0
}

// ===================== Error Message Tests =====================

TEST_F(ConnectionWidgetTest, SetErrorMessageSimple) {
  widget_->setConnectionState(ConnectionState::kError);
  widget_->setErrorMessage("Connection failed");
  // Should display error message
}

TEST_F(ConnectionWidgetTest, SetErrorMessageEmpty) {
  widget_->setConnectionState(ConnectionState::kError);
  widget_->setErrorMessage("");
  // Should handle empty error message
}

TEST_F(ConnectionWidgetTest, SetErrorMessageLong) {
  widget_->setConnectionState(ConnectionState::kError);
  QString longMessage = QString("Error: ").repeated(100);
  widget_->setErrorMessage(longMessage);
  // Should handle long error messages
}

TEST_F(ConnectionWidgetTest, SetStructuredError) {
  widget_->setConnectionState(ConnectionState::kError);
  ErrorMessage error;
  error.title = "Connection Failed";
  error.description = "Unable to establish secure connection";
  error.category = ErrorCategory::kNetwork;
  widget_->setError(error);
  // Should display structured error
}

TEST_F(ConnectionWidgetTest, SetErrorWithDetails) {
  widget_->setConnectionState(ConnectionState::kError);
  ErrorMessage error;
  error.title = "Authentication Failed";
  error.description = "Invalid credentials";
  error.technical_details = "Server returned 401 Unauthorized";
  error.category = ErrorCategory::kConfiguration;
  widget_->setError(error);
  // Should display error with details
}

TEST_F(ConnectionWidgetTest, SetErrorWithAction) {
  widget_->setConnectionState(ConnectionState::kError);
  ErrorMessage error;
  error.title = "Network Unreachable";
  error.description = "Cannot reach VPN server";
  error.action = "Check your internet connection";
  error.category = ErrorCategory::kNetwork;
  widget_->setError(error);
  // Should display error with action text
}

// ===================== Signal Emission Tests =====================

TEST_F(ConnectionWidgetTest, ConnectRequestedSignalEmitted) {
  QSignalSpy spy(widget_, &ConnectionWidget::connectRequested);
  widget_->setConnectionState(ConnectionState::kDisconnected);
  widget_->onConnectClicked();
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(ConnectionWidgetTest, DisconnectRequestedSignalEmitted) {
  QSignalSpy spy(widget_, &ConnectionWidget::disconnectRequested);
  widget_->setConnectionState(ConnectionState::kConnected);
  widget_->onConnectClicked();
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(ConnectionWidgetTest, SettingsRequestedSignal) {
  QSignalSpy spy(widget_, &ConnectionWidget::settingsRequested);
  // Would need to trigger settings button click via UI
  // (This requires finding the settings button)
}

TEST_F(ConnectionWidgetTest, ServersRequestedSignal) {
  QSignalSpy spy(widget_, &ConnectionWidget::serversRequested);
  // Would need to trigger servers button click
}

TEST_F(ConnectionWidgetTest, DiagnosticsRequestedSignal) {
  QSignalSpy spy(widget_, &ConnectionWidget::diagnosticsRequested);
  // Would need to trigger diagnostics button click
}

// ===================== Settings Load Tests =====================

TEST_F(ConnectionWidgetTest, LoadServerSettingsEmpty) {
  widget_->loadServerSettings();
  // Should not crash with no settings
}

TEST_F(ConnectionWidgetTest, LoadServerSettingsWithData) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "vpn.example.com");
  settings.setValue("server/port", 4433);
  settings.sync();

  widget_->loadServerSettings();
  // Should load settings successfully
}

TEST_F(ConnectionWidgetTest, LoadServerSettingsInvalidPort) {
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "vpn.example.com");
  settings.setValue("server/port", -1);
  settings.sync();

  widget_->loadServerSettings();
  // Should handle invalid port gracefully
}

// ===================== Connect Button State Tests =====================

TEST_F(ConnectionWidgetTest, OnConnectClickedFromDisconnected) {
  QSignalSpy connectSpy(widget_, &ConnectionWidget::connectRequested);
  QSignalSpy disconnectSpy(widget_, &ConnectionWidget::disconnectRequested);

  widget_->setConnectionState(ConnectionState::kDisconnected);
  widget_->onConnectClicked();

  EXPECT_EQ(connectSpy.count(), 1);
  EXPECT_EQ(disconnectSpy.count(), 0);
}

TEST_F(ConnectionWidgetTest, OnConnectClickedFromConnected) {
  QSignalSpy connectSpy(widget_, &ConnectionWidget::connectRequested);
  QSignalSpy disconnectSpy(widget_, &ConnectionWidget::disconnectRequested);

  widget_->setConnectionState(ConnectionState::kConnected);
  widget_->onConnectClicked();

  EXPECT_EQ(connectSpy.count(), 0);
  EXPECT_EQ(disconnectSpy.count(), 1);
}

TEST_F(ConnectionWidgetTest, OnConnectClickedFromConnecting) {
  QSignalSpy connectSpy(widget_, &ConnectionWidget::connectRequested);
  QSignalSpy disconnectSpy(widget_, &ConnectionWidget::disconnectRequested);

  widget_->setConnectionState(ConnectionState::kConnecting);
  widget_->onConnectClicked();

  // Should emit disconnect to cancel connection attempt
  EXPECT_EQ(connectSpy.count(), 0);
  EXPECT_EQ(disconnectSpy.count(), 1);
}

TEST_F(ConnectionWidgetTest, OnConnectClickedFromError) {
  QSignalSpy connectSpy(widget_, &ConnectionWidget::connectRequested);
  QSignalSpy disconnectSpy(widget_, &ConnectionWidget::disconnectRequested);

  widget_->setConnectionState(ConnectionState::kError);
  widget_->onConnectClicked();

  // From error state, should allow reconnect
  EXPECT_EQ(connectSpy.count(), 1);
  EXPECT_EQ(disconnectSpy.count(), 0);
}

// ===================== Multiple State Transition Tests =====================

TEST_F(ConnectionWidgetTest, RapidStateChanges) {
  // Test rapid state changes don't cause crashes
  for (int i = 0; i < 10; ++i) {
    widget_->setConnectionState(ConnectionState::kDisconnected);
    widget_->setConnectionState(ConnectionState::kConnecting);
    widget_->setConnectionState(ConnectionState::kConnected);
  }
}

TEST_F(ConnectionWidgetTest, UpdateMetricsRapidly) {
  widget_->setConnectionState(ConnectionState::kConnected);
  for (int i = 0; i < 100; ++i) {
    widget_->updateMetrics(i % 100, static_cast<uint64_t>(i) * 1024, static_cast<uint64_t>(i) * 512);
  }
}

TEST_F(ConnectionWidgetTest, SessionInfoUpdatesRapidly) {
  for (int i = 0; i < 50; ++i) {
    widget_->setSessionId(QString("session-%1").arg(i));
    widget_->setServerAddress(QString("server-%1.example.com").arg(i), static_cast<uint16_t>(4433 + i));
  }
}

// ===================== Edge Case Tests =====================

TEST_F(ConnectionWidgetTest, SetConnectionStateToSameState) {
  widget_->setConnectionState(ConnectionState::kDisconnected);
  widget_->setConnectionState(ConnectionState::kDisconnected);
  widget_->setConnectionState(ConnectionState::kDisconnected);
  // Should handle setting to same state multiple times
}

TEST_F(ConnectionWidgetTest, UpdateMetricsWithMaxValues) {
  widget_->setConnectionState(ConnectionState::kConnected);
  widget_->updateMetrics(999999, UINT64_MAX, UINT64_MAX);
  // Should handle maximum values
}

TEST_F(ConnectionWidgetTest, SetServerAddressVeryLongHostname) {
  QString longHostname = QString("subdomain.").repeated(50) + "example.com";
  widget_->setServerAddress(longHostname, 4433);
  // Should handle very long hostnames
}

TEST_F(ConnectionWidgetTest, MultipleErrorMessages) {
  widget_->setConnectionState(ConnectionState::kError);
  widget_->setErrorMessage("Error 1");
  widget_->setErrorMessage("Error 2");
  widget_->setErrorMessage("Error 3");
  // Should update error message correctly
}

}  // namespace
}  // namespace veil::gui
// NOLINTEND(readability-implicit-bool-conversion)
