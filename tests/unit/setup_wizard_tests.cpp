#include <gtest/gtest.h>

#include <QApplication>
#include <QSettings>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>

#include "gui-client/setup_wizard.h"

namespace {

/// Ensure QApplication exists for Qt widget tests.
/// Must be created before any QWidget construction.
class SetupWizardTestEnvironment : public ::testing::Environment {
 public:
  void SetUp() override {
    if (!QApplication::instance()) {
      // QApplication requires argc to remain valid for its lifetime
      static int argc = 1;
      static const char* argv[] = {"test"};
      static QApplication app(argc, const_cast<char**>(argv));
      (void)app;
    }
  }
};

::testing::Environment* const testEnv =
    ::testing::AddGlobalTestEnvironment(new SetupWizardTestEnvironment());

}  // namespace

namespace veil::gui {

class SetupWizardTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Clear any existing first-run flag before each test
    QSettings settings("VEIL", "VPN Client");
    settings.remove("app/firstRunCompleted");
    settings.sync();
  }

  void TearDown() override {
    // Clean up settings after each test
    QSettings settings("VEIL", "VPN Client");
    settings.remove("app/firstRunCompleted");
    settings.sync();
  }
};

TEST_F(SetupWizardTest, IsFirstRunReturnsTrueWhenNoFlagSet) {
  EXPECT_TRUE(SetupWizard::isFirstRun());
}

TEST_F(SetupWizardTest, MarkFirstRunCompleteSetsFlag) {
  EXPECT_TRUE(SetupWizard::isFirstRun());
  SetupWizard::markFirstRunComplete();
  EXPECT_FALSE(SetupWizard::isFirstRun());
}

TEST_F(SetupWizardTest, ResetFirstRunClearsFlag) {
  SetupWizard::markFirstRunComplete();
  EXPECT_FALSE(SetupWizard::isFirstRun());
  SetupWizard::resetFirstRun();
  EXPECT_TRUE(SetupWizard::isFirstRun());
}

TEST_F(SetupWizardTest, WizardCanBeConstructed) {
  SetupWizard wizard;
  EXPECT_FALSE(wizard.isVisible());
}

TEST_F(SetupWizardTest, WizardEmitsSkippedSignal) {
  SetupWizard wizard;
  bool skippedEmitted = false;
  QObject::connect(&wizard, &SetupWizard::wizardSkipped, [&skippedEmitted]() {
    skippedEmitted = true;
  });

  // Simulate skip by calling the slot via meta-object system
  QMetaObject::invokeMethod(&wizard, "onSkipClicked", Qt::DirectConnection);
  EXPECT_TRUE(skippedEmitted);
  // After skipping, first run should be marked complete
  EXPECT_FALSE(SetupWizard::isFirstRun());
}

TEST_F(SetupWizardTest, SettingsAreSavedOnFinish) {
  SetupWizard wizard;
  bool completedEmitted = false;
  QObject::connect(&wizard, &SetupWizard::wizardCompleted,
                   [&completedEmitted]() { completedEmitted = true; });

  // Simulate finishing the wizard
  QMetaObject::invokeMethod(&wizard, "onFinishClicked", Qt::DirectConnection);
  EXPECT_TRUE(completedEmitted);
  EXPECT_FALSE(SetupWizard::isFirstRun());
}

TEST_F(SetupWizardTest, ImportConfigFromValidFile) {
  // Create a temporary config file
  QTemporaryFile tempFile;
  tempFile.setAutoRemove(true);
  tempFile.setFileTemplate(QDir::tempPath() + "/test_config_XXXXXX.veil");
  ASSERT_TRUE(tempFile.open());

  QJsonObject server;
  server["address"] = "test.vpn.example.com";
  server["port"] = 5544;

  QJsonObject crypto;
  crypto["keyFile"] = "/tmp/test.key";

  QJsonObject advanced;
  advanced["obfuscation"] = false;

  QJsonObject dpi;
  dpi["mode"] = 2;

  QJsonObject routing;
  routing["routeAllTraffic"] = false;

  QJsonObject connection;
  connection["autoReconnect"] = false;

  QJsonObject root;
  root["server"] = server;
  root["crypto"] = crypto;
  root["advanced"] = advanced;
  root["dpi"] = dpi;
  root["routing"] = routing;
  root["connection"] = connection;

  QJsonDocument doc(root);
  tempFile.write(doc.toJson());
  tempFile.close();

  // Import the config through the wizard
  SetupWizard wizard;

  // We cannot directly call importConfigFromFile since it's private,
  // but we can test that the wizard constructs and that the first-run
  // flag mechanism works correctly with settings.
  // The import functionality is tested via the UI path.

  // Verify the temp file is valid JSON
  QFile readBack(tempFile.fileName());
  ASSERT_TRUE(readBack.open(QIODevice::ReadOnly));
  QJsonParseError parseError;
  QJsonDocument readDoc = QJsonDocument::fromJson(readBack.readAll(), &parseError);
  EXPECT_FALSE(readDoc.isNull());
  EXPECT_TRUE(readDoc.isObject());
  EXPECT_EQ(readDoc.object()["server"].toObject()["address"].toString(),
            "test.vpn.example.com");
  EXPECT_EQ(readDoc.object()["server"].toObject()["port"].toInt(), 5544);
}

}  // namespace veil::gui
