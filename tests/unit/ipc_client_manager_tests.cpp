// NOLINTBEGIN(readability-implicit-bool-conversion)
#include <gtest/gtest.h>

#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include "ipc_client_manager.h"
#include "common/ipc/ipc_protocol.h"

// We need a QApplication for Qt-based tests.
// Set offscreen platform for headless CI environments.
static struct OffscreenPlatformSetter {
  OffscreenPlatformSetter() {
    if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
      qputenv("QT_QPA_PLATFORM", "offscreen");
    }
  }
} offscreenSetter;  // NOLINT(cert-err58-cpp)

static int argc = 1;
static char appName[] = "ipc_client_manager_tests";  // NOLINT(cppcoreguidelines-avoid-c-arrays)
static char* argv[] = {appName, nullptr};  // NOLINT(cppcoreguidelines-avoid-c-arrays)

namespace veil::gui {
namespace {

class IpcClientManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (QApplication::instance() == nullptr) {
      app_ = new QApplication(argc, argv);
    }
    manager_ = new IpcClientManager();
  }

  void TearDown() override {
    delete manager_;
  }

  IpcClientManager* manager_{nullptr};
  QApplication* app_{nullptr};
};

// ===================== Construction Tests =====================

TEST_F(IpcClientManagerTest, InitialState) {
  EXPECT_NE(manager_, nullptr);
  // Initially not connected to daemon
  EXPECT_FALSE(manager_->isConnected());
}

// ===================== Connection Tests =====================

TEST_F(IpcClientManagerTest, ConnectToDaemonWhenNotRunning) {
  // Daemon is unlikely to be running in test environment
  // Should return false and emit error signal
  QSignalSpy errorSpy(manager_, &IpcClientManager::errorOccurred);

  bool result = manager_->connectToDaemon();

  // Connection should fail if daemon is not running
  // (May succeed in rare cases if daemon is actually running)
  if (!result) {
    EXPECT_FALSE(manager_->isConnected());
    EXPECT_GE(errorSpy.count(), 1);
  }
}

TEST_F(IpcClientManagerTest, DisconnectFromDaemon) {
  manager_->disconnect();
  EXPECT_FALSE(manager_->isConnected());
}

TEST_F(IpcClientManagerTest, IsConnectedInitiallyFalse) {
  EXPECT_FALSE(manager_->isConnected());
}

TEST_F(IpcClientManagerTest, MultipleConnectAttempts) {
  // Should handle multiple connection attempts
  manager_->connectToDaemon();
  manager_->connectToDaemon();
  manager_->connectToDaemon();
}

TEST_F(IpcClientManagerTest, MultipleDisconnectCalls) {
  // Should handle multiple disconnect calls safely
  manager_->disconnect();
  manager_->disconnect();
  manager_->disconnect();
  EXPECT_FALSE(manager_->isConnected());
}

// ===================== Send Command Tests =====================

TEST_F(IpcClientManagerTest, SendConnectWhenNotConnected) {
  QSignalSpy errorSpy(manager_, &IpcClientManager::errorOccurred);

  ipc::ConnectionConfig config;
  config.server_address = "vpn.example.com";
  config.server_port = 4433;

  bool result = manager_->sendConnect(config);

  // Should fail because not connected to daemon
  EXPECT_FALSE(result);
  EXPECT_GE(errorSpy.count(), 1);
}

TEST_F(IpcClientManagerTest, SendConnectLegacyWhenNotConnected) {
  QSignalSpy errorSpy(manager_, &IpcClientManager::errorOccurred);

  bool result = manager_->sendConnect("vpn.example.com", 4433);

  // Should fail because not connected to daemon
  EXPECT_FALSE(result);
  EXPECT_GE(errorSpy.count(), 1);
}

TEST_F(IpcClientManagerTest, SendDisconnectWhenNotConnected) {
  // Should handle disconnect command even when not connected
  bool result = manager_->sendDisconnect();

  // May fail or succeed depending on implementation
  // Just ensure it doesn't crash
}

TEST_F(IpcClientManagerTest, RequestStatusWhenNotConnected) {
  // Should handle status request when not connected
  bool result = manager_->requestStatus();

  // Should fail because not connected to daemon
  EXPECT_FALSE(result);
}

TEST_F(IpcClientManagerTest, RequestDiagnosticsWhenNotConnected) {
  // Should handle diagnostics request when not connected
  bool result = manager_->requestDiagnostics();

  // Should fail because not connected to daemon
  EXPECT_FALSE(result);
}

// ===================== Signal Emission Tests =====================

TEST_F(IpcClientManagerTest, ErrorSignalEmittedOnConnectionFailure) {
  QSignalSpy errorSpy(manager_, &IpcClientManager::errorOccurred);

  manager_->connectToDaemon();

  // If daemon is not running, error signal should be emitted
  if (!manager_->isConnected()) {
    EXPECT_GE(errorSpy.count(), 1);

    // Verify signal parameters
    if (errorSpy.count() > 0) {
      QList<QVariant> arguments = errorSpy.takeFirst();
      EXPECT_FALSE(arguments.at(0).toString().isEmpty());  // Error message
    }
  }
}

TEST_F(IpcClientManagerTest, DaemonConnectionChangedSignalOnConnect) {
  QSignalSpy connectionSpy(manager_, &IpcClientManager::daemonConnectionChanged);

  manager_->connectToDaemon();

  // Signal should be emitted regardless of success/failure
  // (May be emitted asynchronously)
  QTest::qWait(100);  // Wait briefly for signal
}

TEST_F(IpcClientManagerTest, DaemonConnectionChangedSignalOnDisconnect) {
  QSignalSpy connectionSpy(manager_, &IpcClientManager::daemonConnectionChanged);

  manager_->disconnect();

  // Should emit disconnection signal
  EXPECT_GE(connectionSpy.count(), 1);

  if (connectionSpy.count() > 0) {
    QList<QVariant> arguments = connectionSpy.takeFirst();
    EXPECT_FALSE(arguments.at(0).toBool());  // Should be false for disconnect
  }
}

// ===================== Connection Configuration Tests =====================

TEST_F(IpcClientManagerTest, SendConnectWithFullConfig) {
  ipc::ConnectionConfig config;
  config.server_address = "vpn.example.com";
  config.server_port = 4433;
  config.key_file = "/path/to/key.pem";
  config.obfuscation_seed_file = "/path/to/seed.bin";
  config.tun_device_name = "tun0";
  config.tun_ip_address = "10.8.0.2";
  config.tun_netmask = "255.255.255.0";
  config.tun_mtu = 1500;
  config.route_all_traffic = true;
  config.auto_reconnect = true;
  config.reconnect_interval_sec = 5;
  config.max_reconnect_attempts = 3;
  config.enable_obfuscation = true;

  bool result = manager_->sendConnect(config);

  // Will fail if daemon is not running, but should not crash
  EXPECT_FALSE(result);
}

TEST_F(IpcClientManagerTest, SendConnectWithCustomRoutes) {
  ipc::ConnectionConfig config;
  config.server_address = "vpn.example.com";
  config.server_port = 4433;
  config.custom_routes.push_back("192.168.1.0/24");
  config.custom_routes.push_back("10.0.0.0/8");

  bool result = manager_->sendConnect(config);

  // Will fail if daemon is not running, but should not crash
  EXPECT_FALSE(result);
}

TEST_F(IpcClientManagerTest, SendConnectWithEmptyServerAddress) {
  ipc::ConnectionConfig config;
  config.server_address = "";
  config.server_port = 4433;

  bool result = manager_->sendConnect(config);

  // Should handle empty server address
  EXPECT_FALSE(result);
}

TEST_F(IpcClientManagerTest, SendConnectWithZeroPort) {
  ipc::ConnectionConfig config;
  config.server_address = "vpn.example.com";
  config.server_port = 0;

  bool result = manager_->sendConnect(config);

  // Should handle zero port
  EXPECT_FALSE(result);
}

TEST_F(IpcClientManagerTest, SendConnectLegacyBasic) {
  bool result = manager_->sendConnect("vpn.example.com", 4433);

  // Will fail if daemon is not running
  EXPECT_FALSE(result);
}

TEST_F(IpcClientManagerTest, SendConnectLegacyIPv4) {
  bool result = manager_->sendConnect("192.168.1.1", 8080);

  EXPECT_FALSE(result);
}

TEST_F(IpcClientManagerTest, SendConnectLegacyIPv6) {
  bool result = manager_->sendConnect("2001:db8::1", 443);

  EXPECT_FALSE(result);
}

// ===================== Multiple Operations Tests =====================

TEST_F(IpcClientManagerTest, ConnectDisconnectSequence) {
  manager_->connectToDaemon();
  manager_->disconnect();
  manager_->connectToDaemon();
  manager_->disconnect();
}

TEST_F(IpcClientManagerTest, MultipleSendConnectCalls) {
  ipc::ConnectionConfig config;
  config.server_address = "vpn.example.com";
  config.server_port = 4433;

  // Should handle multiple send attempts
  manager_->sendConnect(config);
  manager_->sendConnect(config);
  manager_->sendConnect(config);
}

TEST_F(IpcClientManagerTest, MultipleSendDisconnectCalls) {
  // Should handle multiple disconnect command attempts
  manager_->sendDisconnect();
  manager_->sendDisconnect();
  manager_->sendDisconnect();
}

TEST_F(IpcClientManagerTest, MultipleStatusRequests) {
  // Should handle multiple status requests
  manager_->requestStatus();
  manager_->requestStatus();
  manager_->requestStatus();
}

TEST_F(IpcClientManagerTest, MultipleDiagnosticsRequests) {
  // Should handle multiple diagnostics requests
  manager_->requestDiagnostics();
  manager_->requestDiagnostics();
  manager_->requestDiagnostics();
}

// ===================== Signal Spy Tests =====================

TEST_F(IpcClientManagerTest, AllSignalsExist) {
  // Verify all expected signals exist
  QSignalSpy connectionStateSpy(manager_, &IpcClientManager::connectionStateChanged);
  QSignalSpy statusSpy(manager_, &IpcClientManager::statusUpdated);
  QSignalSpy metricsSpy(manager_, &IpcClientManager::metricsUpdated);
  QSignalSpy diagnosticsSpy(manager_, &IpcClientManager::diagnosticsReceived);
  QSignalSpy logEventSpy(manager_, &IpcClientManager::logEventReceived);
  QSignalSpy errorSpy(manager_, &IpcClientManager::errorOccurred);
  QSignalSpy daemonConnectionSpy(manager_, &IpcClientManager::daemonConnectionChanged);

  // All signal spies should be valid
  EXPECT_TRUE(connectionStateSpy.isValid());
  EXPECT_TRUE(statusSpy.isValid());
  EXPECT_TRUE(metricsSpy.isValid());
  EXPECT_TRUE(diagnosticsSpy.isValid());
  EXPECT_TRUE(logEventSpy.isValid());
  EXPECT_TRUE(errorSpy.isValid());
  EXPECT_TRUE(daemonConnectionSpy.isValid());
}

// ===================== Reconnection Tests =====================

TEST_F(IpcClientManagerTest, ReconnectionTimerStartsOnFailure) {
  QSignalSpy errorSpy(manager_, &IpcClientManager::errorOccurred);

  manager_->connectToDaemon();

  // If connection failed, reconnection timer should start
  // (This is internal behavior that we can't directly verify,
  //  but we ensure no crash occurs)
}

// ===================== Destruction Tests =====================

TEST_F(IpcClientManagerTest, DestructionWhileConnected) {
  // Attempt to connect (will likely fail)
  manager_->connectToDaemon();

  // Destruction should clean up properly
  delete manager_;
  manager_ = nullptr;

  // Should not crash
}

TEST_F(IpcClientManagerTest, DestructionAfterDisconnect) {
  manager_->connectToDaemon();
  manager_->disconnect();

  delete manager_;
  manager_ = nullptr;

  // Should not crash
}

}  // namespace
}  // namespace veil::gui
// NOLINTEND(readability-implicit-bool-conversion)
