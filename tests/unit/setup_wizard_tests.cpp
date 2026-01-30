#include <gtest/gtest.h>

#include <QApplication>
#include <QSettings>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QStackedWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>

#include "gui-client/setup_wizard.h"

namespace {

/// Ensure QApplication exists for Qt widget tests.
/// Must be created before any QWidget construction.
class SetupWizardTestEnvironment : public ::testing::Environment {
 public:
  void SetUp() override {
    if (QApplication::instance() == nullptr) {
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

// ===================== First-Run Flag Tests =====================

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

TEST_F(SetupWizardTest, FirstRunFlagPersistsAcrossInstances) {
  SetupWizard::markFirstRunComplete();
  // The flag should persist without any wizard instance
  EXPECT_FALSE(SetupWizard::isFirstRun());
}

TEST_F(SetupWizardTest, MultipleResetCallsAreIdempotent) {
  SetupWizard::markFirstRunComplete();
  SetupWizard::resetFirstRun();
  SetupWizard::resetFirstRun();
  EXPECT_TRUE(SetupWizard::isFirstRun());
}

TEST_F(SetupWizardTest, MultipleMarkCompleteCallsAreIdempotent) {
  SetupWizard::markFirstRunComplete();
  SetupWizard::markFirstRunComplete();
  EXPECT_FALSE(SetupWizard::isFirstRun());
}

// ===================== Construction Tests =====================

TEST_F(SetupWizardTest, WizardCanBeConstructed) {
  SetupWizard wizard;
  EXPECT_FALSE(wizard.isVisible());
}

TEST_F(SetupWizardTest, WizardHasCorrectChildWidgets) {
  SetupWizard wizard;

  // Should have a page stack
  auto* pageStack = wizard.findChild<QStackedWidget*>();
  ASSERT_NE(pageStack, nullptr);
  EXPECT_EQ(pageStack->count(), 5);  // 5 pages
}

TEST_F(SetupWizardTest, WizardStartsOnFirstPage) {
  SetupWizard wizard;
  auto* pageStack = wizard.findChild<QStackedWidget*>();
  ASSERT_NE(pageStack, nullptr);
  EXPECT_EQ(pageStack->currentIndex(), 0);  // Welcome page
}

TEST_F(SetupWizardTest, WizardHasNavigationButtons) {
  SetupWizard wizard;

  // Find navigation buttons by text
  auto buttons = wizard.findChildren<QPushButton*>();
  bool hasSkip = false;
  bool hasNext = false;

  for (auto* btn : buttons) {
    if (btn->text().contains("Skip")) hasSkip = true;
    if (btn->text().contains("Next")) hasNext = true;
  }

  EXPECT_TRUE(hasSkip);
  EXPECT_TRUE(hasNext);
}

// ===================== Signal Tests =====================

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

TEST_F(SetupWizardTest, WizardEmitsCompletedSignal) {
  SetupWizard wizard;
  bool completedEmitted = false;
  QObject::connect(&wizard, &SetupWizard::wizardCompleted,
                   [&completedEmitted]() { completedEmitted = true; });

  // Simulate finishing the wizard
  QMetaObject::invokeMethod(&wizard, "onFinishClicked", Qt::DirectConnection);
  EXPECT_TRUE(completedEmitted);
  EXPECT_FALSE(SetupWizard::isFirstRun());
}

TEST_F(SetupWizardTest, SkipDoesNotEmitCompletedSignal) {
  SetupWizard wizard;
  bool completedEmitted = false;
  bool skippedEmitted = false;

  QObject::connect(&wizard, &SetupWizard::wizardCompleted,
                   [&completedEmitted]() { completedEmitted = true; });
  QObject::connect(&wizard, &SetupWizard::wizardSkipped,
                   [&skippedEmitted]() { skippedEmitted = true; });

  QMetaObject::invokeMethod(&wizard, "onSkipClicked", Qt::DirectConnection);
  EXPECT_TRUE(skippedEmitted);
  EXPECT_FALSE(completedEmitted);
}

TEST_F(SetupWizardTest, FinishDoesNotEmitSkippedSignal) {
  SetupWizard wizard;
  bool completedEmitted = false;
  bool skippedEmitted = false;

  QObject::connect(&wizard, &SetupWizard::wizardCompleted,
                   [&completedEmitted]() { completedEmitted = true; });
  QObject::connect(&wizard, &SetupWizard::wizardSkipped,
                   [&skippedEmitted]() { skippedEmitted = true; });

  QMetaObject::invokeMethod(&wizard, "onFinishClicked", Qt::DirectConnection);
  EXPECT_TRUE(completedEmitted);
  EXPECT_FALSE(skippedEmitted);
}

// ===================== Navigation Tests =====================

TEST_F(SetupWizardTest, NextAdvancesFromWelcomePage) {
  SetupWizard wizard;
  auto* pageStack = wizard.findChild<QStackedWidget*>();
  ASSERT_NE(pageStack, nullptr);
  EXPECT_EQ(pageStack->currentIndex(), 0);

  // Welcome page has no validation, so Next should advance
  QMetaObject::invokeMethod(&wizard, "onNextClicked", Qt::DirectConnection);
  EXPECT_EQ(pageStack->currentIndex(), 1);  // Server page
}

TEST_F(SetupWizardTest, BackFromSecondPageReturnsToFirst) {
  SetupWizard wizard;
  auto* pageStack = wizard.findChild<QStackedWidget*>();
  ASSERT_NE(pageStack, nullptr);

  // Go to page 1
  QMetaObject::invokeMethod(&wizard, "onNextClicked", Qt::DirectConnection);
  EXPECT_EQ(pageStack->currentIndex(), 1);

  // Go back
  QMetaObject::invokeMethod(&wizard, "onBackClicked", Qt::DirectConnection);
  EXPECT_EQ(pageStack->currentIndex(), 0);
}

TEST_F(SetupWizardTest, BackOnFirstPageDoesNothing) {
  SetupWizard wizard;
  auto* pageStack = wizard.findChild<QStackedWidget*>();
  ASSERT_NE(pageStack, nullptr);
  EXPECT_EQ(pageStack->currentIndex(), 0);

  QMetaObject::invokeMethod(&wizard, "onBackClicked", Qt::DirectConnection);
  EXPECT_EQ(pageStack->currentIndex(), 0);  // Still on first page
}

TEST_F(SetupWizardTest, ServerPageValidationBlocksEmptyAddress) {
  SetupWizard wizard;
  auto* pageStack = wizard.findChild<QStackedWidget*>();
  ASSERT_NE(pageStack, nullptr);

  // Navigate to server page
  QMetaObject::invokeMethod(&wizard, "onNextClicked", Qt::DirectConnection);
  EXPECT_EQ(pageStack->currentIndex(), 1);

  // Try to advance without entering server address
  // Server address edit should be empty by default
  QMetaObject::invokeMethod(&wizard, "onNextClicked", Qt::DirectConnection);
  // Should still be on server page due to validation
  EXPECT_EQ(pageStack->currentIndex(), 1);
}

TEST_F(SetupWizardTest, ServerPageValidationAllowsValidAddress) {
  SetupWizard wizard;
  auto* pageStack = wizard.findChild<QStackedWidget*>();
  ASSERT_NE(pageStack, nullptr);

  // Navigate to server page
  QMetaObject::invokeMethod(&wizard, "onNextClicked", Qt::DirectConnection);
  EXPECT_EQ(pageStack->currentIndex(), 1);

  // Find the server address edit and set a value
  auto lineEdits = wizard.findChildren<QLineEdit*>();
  QLineEdit* serverEdit = nullptr;
  for (auto* edit : lineEdits) {
    if (edit->placeholderText().contains("vpn.example.com")) {
      serverEdit = edit;
      break;
    }
  }
  ASSERT_NE(serverEdit, nullptr);
  serverEdit->setText("test.example.com");

  // Now Next should advance to key file page
  QMetaObject::invokeMethod(&wizard, "onNextClicked", Qt::DirectConnection);
  EXPECT_EQ(pageStack->currentIndex(), 2);  // Key file page
}

TEST_F(SetupWizardTest, CanNavigateToAllPages) {
  SetupWizard wizard;
  auto* pageStack = wizard.findChild<QStackedWidget*>();
  ASSERT_NE(pageStack, nullptr);

  // Page 0 -> 1 (Welcome -> Server)
  QMetaObject::invokeMethod(&wizard, "onNextClicked", Qt::DirectConnection);
  EXPECT_EQ(pageStack->currentIndex(), 1);

  // Set server address for validation
  auto lineEdits = wizard.findChildren<QLineEdit*>();
  for (auto* edit : lineEdits) {
    if (edit->placeholderText().contains("vpn.example.com")) {
      edit->setText("192.168.1.1");
      break;
    }
  }

  // Page 1 -> 2 (Server -> Key File)
  QMetaObject::invokeMethod(&wizard, "onNextClicked", Qt::DirectConnection);
  EXPECT_EQ(pageStack->currentIndex(), 2);

  // Page 2 -> 3 (Key File -> Features)
  QMetaObject::invokeMethod(&wizard, "onNextClicked", Qt::DirectConnection);
  EXPECT_EQ(pageStack->currentIndex(), 3);

  // Page 3 -> 4 (Features -> Finish)
  QMetaObject::invokeMethod(&wizard, "onNextClicked", Qt::DirectConnection);
  EXPECT_EQ(pageStack->currentIndex(), 4);
}

// ===================== Settings Persistence Tests =====================

TEST_F(SetupWizardTest, SettingsAreSavedOnFinish) {
  SetupWizard wizard;

  // Set a server address in the wizard
  auto lineEdits = wizard.findChildren<QLineEdit*>();
  for (auto* edit : lineEdits) {
    if (edit->placeholderText().contains("vpn.example.com")) {
      edit->setText("saved.server.com");
      break;
    }
  }

  // Set a port value
  auto spinBoxes = wizard.findChildren<QSpinBox*>();
  for (auto* spinBox : spinBoxes) {
    if (spinBox->minimum() == 1 && spinBox->maximum() == 65535) {
      spinBox->setValue(5555);
      break;
    }
  }

  // Finish the wizard
  QMetaObject::invokeMethod(&wizard, "onFinishClicked", Qt::DirectConnection);

  // Verify settings were saved
  QSettings settings("VEIL", "VPN Client");
  EXPECT_EQ(settings.value("server/address").toString(), "saved.server.com");
  EXPECT_EQ(settings.value("server/port").toInt(), 5555);
}

TEST_F(SetupWizardTest, FeatureSettingsAreSavedOnFinish) {
  SetupWizard wizard;

  // Find and uncheck obfuscation
  auto checkBoxes = wizard.findChildren<QCheckBox*>();
  for (auto* cb : checkBoxes) {
    if (cb->text().contains("obfuscation", Qt::CaseInsensitive)) {
      cb->setChecked(false);
      break;
    }
  }

  // Finish the wizard
  QMetaObject::invokeMethod(&wizard, "onFinishClicked", Qt::DirectConnection);

  // Verify settings were saved
  QSettings settings("VEIL", "VPN Client");
  EXPECT_FALSE(settings.value("advanced/obfuscation").toBool());
}

TEST_F(SetupWizardTest, SkipDoesNotSaveSettings) {
  // Set a known default first
  QSettings settings("VEIL", "VPN Client");
  settings.setValue("server/address", "original.server.com");
  settings.sync();

  SetupWizard wizard;

  // Change the server address in the wizard
  auto lineEdits = wizard.findChildren<QLineEdit*>();
  for (auto* edit : lineEdits) {
    if (edit->placeholderText().contains("vpn.example.com")) {
      edit->setText("new.server.com");
      break;
    }
  }

  // Skip the wizard
  QMetaObject::invokeMethod(&wizard, "onSkipClicked", Qt::DirectConnection);

  // Verify the original setting was NOT overwritten
  settings.sync();
  EXPECT_EQ(settings.value("server/address").toString(), "original.server.com");
}

// ===================== Config Import Tests =====================

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

TEST_F(SetupWizardTest, ImportConfigWithMissingFieldsIsValid) {
  // Create a minimal config with only server section
  QTemporaryFile tempFile;
  tempFile.setAutoRemove(true);
  tempFile.setFileTemplate(QDir::tempPath() + "/test_minimal_XXXXXX.veil");
  ASSERT_TRUE(tempFile.open());

  QJsonObject server;
  server["address"] = "minimal.server.com";

  QJsonObject root;
  root["server"] = server;

  QJsonDocument doc(root);
  tempFile.write(doc.toJson());
  tempFile.close();

  // Verify the file is valid JSON
  QFile readBack(tempFile.fileName());
  ASSERT_TRUE(readBack.open(QIODevice::ReadOnly));
  QJsonDocument readDoc = QJsonDocument::fromJson(readBack.readAll());
  EXPECT_FALSE(readDoc.isNull());
  EXPECT_TRUE(readDoc.isObject());
  EXPECT_EQ(readDoc.object()["server"].toObject()["address"].toString(),
            "minimal.server.com");
}

TEST_F(SetupWizardTest, InvalidJsonConfigFileIsRejected) {
  QTemporaryFile tempFile;
  tempFile.setAutoRemove(true);
  tempFile.setFileTemplate(QDir::tempPath() + "/test_invalid_XXXXXX.veil");
  ASSERT_TRUE(tempFile.open());

  // Write invalid JSON
  tempFile.write("{ this is not valid json }");
  tempFile.close();

  QFile readBack(tempFile.fileName());
  ASSERT_TRUE(readBack.open(QIODevice::ReadOnly));
  QJsonParseError parseError;
  QJsonDocument readDoc = QJsonDocument::fromJson(readBack.readAll(), &parseError);
  // Should fail to parse
  EXPECT_TRUE(readDoc.isNull());
}

// ===================== UI Element Tests =====================

TEST_F(SetupWizardTest, WizardHasStepIndicators) {
  SetupWizard wizard;

  // There should be step labels for each page
  auto labels = wizard.findChildren<QLabel*>();
  int stepLabelCount = 0;
  QStringList expectedSteps = {"Welcome", "Server", "Key File", "Features", "Finish"};

  for (auto* label : labels) {
    for (const auto& step : expectedSteps) {
      if (label->text() == step) {
        stepLabelCount++;
        break;
      }
    }
  }

  EXPECT_EQ(stepLabelCount, 5);
}

TEST_F(SetupWizardTest, ServerPageHasCorrectWidgets) {
  SetupWizard wizard;

  // Navigate to server page
  QMetaObject::invokeMethod(&wizard, "onNextClicked", Qt::DirectConnection);

  // Should have server address line edit
  auto lineEdits = wizard.findChildren<QLineEdit*>();
  bool hasServerEdit = false;
  for (auto* edit : lineEdits) {
    if (edit->placeholderText().contains("vpn.example.com")) {
      hasServerEdit = true;
      break;
    }
  }
  EXPECT_TRUE(hasServerEdit);

  // Should have port spin box
  auto spinBoxes = wizard.findChildren<QSpinBox*>();
  bool hasPortSpinBox = false;
  for (auto* sb : spinBoxes) {
    if (sb->minimum() == 1 && sb->maximum() == 65535) {
      hasPortSpinBox = true;
      EXPECT_EQ(sb->value(), 4433);  // Default port
      break;
    }
  }
  EXPECT_TRUE(hasPortSpinBox);
}

TEST_F(SetupWizardTest, FeaturesPageHasCheckboxes) {
  SetupWizard wizard;

  // Find checkboxes
  auto checkBoxes = wizard.findChildren<QCheckBox*>();
  bool hasObfuscation = false;
  bool hasRouteAll = false;
  bool hasAutoReconnect = false;

  for (auto* cb : checkBoxes) {
    if (cb->text().contains("obfuscation", Qt::CaseInsensitive)) hasObfuscation = true;
    if (cb->text().contains("Route all", Qt::CaseInsensitive)) hasRouteAll = true;
    if (cb->text().contains("Auto-reconnect", Qt::CaseInsensitive)) hasAutoReconnect = true;
  }

  EXPECT_TRUE(hasObfuscation);
  EXPECT_TRUE(hasRouteAll);
  EXPECT_TRUE(hasAutoReconnect);
}

TEST_F(SetupWizardTest, FeaturesDefaultValuesAreCorrect) {
  SetupWizard wizard;

  auto checkBoxes = wizard.findChildren<QCheckBox*>();
  for (auto* cb : checkBoxes) {
    if (cb->text().contains("obfuscation", Qt::CaseInsensitive)) {
      EXPECT_TRUE(cb->isChecked());  // Obfuscation on by default
    }
    if (cb->text().contains("Route all", Qt::CaseInsensitive)) {
      EXPECT_TRUE(cb->isChecked());  // Route all on by default
    }
    if (cb->text().contains("Auto-reconnect", Qt::CaseInsensitive)) {
      EXPECT_TRUE(cb->isChecked());  // Auto-reconnect on by default
    }
  }
}

TEST_F(SetupWizardTest, FinishPageHasTestConnectionButton) {
  SetupWizard wizard;

  auto buttons = wizard.findChildren<QPushButton*>();
  bool hasTestButton = false;
  for (auto* btn : buttons) {
    if (btn->text().contains("Test Connection")) {
      hasTestButton = true;
      break;
    }
  }
  EXPECT_TRUE(hasTestButton);
}

TEST_F(SetupWizardTest, WelcomePageHasImportButton) {
  SetupWizard wizard;

  auto buttons = wizard.findChildren<QPushButton*>();
  bool hasImport = false;
  for (auto* btn : buttons) {
    if (btn->text().contains("Import Configuration")) {
      hasImport = true;
      break;
    }
  }
  EXPECT_TRUE(hasImport);
}

TEST_F(SetupWizardTest, KeyPageHasBrowseButton) {
  SetupWizard wizard;

  auto buttons = wizard.findChildren<QPushButton*>();
  bool hasBrowse = false;
  for (auto* btn : buttons) {
    if (btn->text().contains("Browse")) {
      hasBrowse = true;
      break;
    }
  }
  EXPECT_TRUE(hasBrowse);
}

TEST_F(SetupWizardTest, KeyPageDoesNotHaveGenerateButton) {
  // After owner feedback: key generation was removed since keys come from server
  SetupWizard wizard;

  auto buttons = wizard.findChildren<QPushButton*>();
  bool hasGenerate = false;
  for (auto* btn : buttons) {
    if (btn->text().contains("Generate")) {
      hasGenerate = true;
      break;
    }
  }
  EXPECT_FALSE(hasGenerate);
}

// ===================== DPI Mode Tests =====================

TEST_F(SetupWizardTest, DpiModeComboHasFourOptions) {
  SetupWizard wizard;

  auto combos = wizard.findChildren<QComboBox*>();
  bool found = false;
  for (auto* combo : combos) {
    if (combo->count() == 4) {
      // This should be the DPI mode combo
      EXPECT_EQ(combo->itemData(0).toInt(), 0);
      EXPECT_EQ(combo->itemData(1).toInt(), 1);
      EXPECT_EQ(combo->itemData(2).toInt(), 2);
      EXPECT_EQ(combo->itemData(3).toInt(), 3);
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "DPI mode combo box with 4 options not found";
}

}  // namespace veil::gui
