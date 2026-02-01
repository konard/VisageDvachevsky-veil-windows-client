#include <QApplication>
#include <QMessageBox>
#include <QDebug>
#include <QTranslator>
#include <QLocale>
#include <QSettings>
#include <QLibraryInfo>

#include <cstdio>
#include <cstdlib>
#include <exception>

#ifdef QT_NETWORK_LIB
#include <QSslSocket>
#endif

#include "mainwindow.h"
#include "common/version.h"

#ifdef _WIN32
#include <windows.h>
#include "windows/service_manager.h"
#endif

namespace {

/// Log file handle for persistent crash diagnostics.
/// Output is written to both stderr and a log file so diagnostics survive
/// even when the console window closes on crash.
FILE* g_logFile = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

/// Custom message handler that flushes output after every Qt debug/warning message.
/// Writes to both stderr (console) and a log file in the app directory.
/// This ensures log output is visible even if the application crashes immediately after.
void flushingMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
  QByteArray localMsg = msg.toLocal8Bit();
  const char* msgStr = localMsg.constData();

  const char* prefix = "";
  switch (type) {
    case QtDebugMsg:    prefix = "";          break;
    case QtInfoMsg:     prefix = "Info: ";    break;
    case QtWarningMsg:  prefix = "Warning: "; break;
    case QtCriticalMsg: prefix = "Critical: "; break;
    case QtFatalMsg:    prefix = "Fatal: ";   break;
  }

  fprintf(stderr, "%s%s\n", prefix, msgStr);
  fflush(stderr);

  if (g_logFile != nullptr) {
    fprintf(g_logFile, "%s%s\n", prefix, msgStr);
    fflush(g_logFile);
  }

  // Suppress unused parameter warning
  (void)context;
}

/// Open log file next to the executable for persistent crash diagnostics.
void openLogFile() {
#ifdef _WIN32
  char path[MAX_PATH];
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  std::string logPath(path);
  auto pos = logPath.find_last_of('\\');
  if (pos != std::string::npos) {
    logPath = logPath.substr(0, pos + 1);
  }
  logPath += "veil-client-gui.log";
  g_logFile = fopen(logPath.c_str(), "w");  // NOLINT(cppcoreguidelines-owning-memory)
  if (g_logFile != nullptr) {
    fprintf(stderr, "Log file: %s\n", logPath.c_str());
    fflush(stderr);
  }
#endif
}

}  // namespace

int main(int argc, char* argv[]) {
  // Open log file and install flushing message handler for crash diagnostics.
  // The log file is written next to the executable so output survives console closing.
  openLogFile();
  qInstallMessageHandler(flushingMessageHandler);

  // Install terminate handler to log crashes that bypass try-catch
  std::set_terminate([]() {
    const char* msg = "FATAL: std::terminate() called — likely an uncaught exception or abort\n";
    fprintf(stderr, "%s", msg);
    fflush(stderr);
    if (g_logFile != nullptr) {
      fprintf(g_logFile, "%s", msg);
      fflush(g_logFile);
    }
    std::abort();
  });

  QApplication app(argc, argv);

  // Log Qt and SSL information for debugging
  qDebug() << "=== VEIL VPN Client Startup ===";
  qDebug() << "Qt Version:" << qVersion();
  qDebug() << "Application Version:" << veil::kVersionString;

#ifdef QT_NETWORK_LIB
  // Check and log SSL/TLS backend support
  qDebug() << "Qt Network SSL Support:" << QSslSocket::supportsSsl();
  if (QSslSocket::supportsSsl()) {
    qDebug() << "SSL Library Build Version:" << QSslSocket::sslLibraryBuildVersionString();
    qDebug() << "SSL Library Runtime Version:" << QSslSocket::sslLibraryVersionString();
  } else {
    qWarning() << "WARNING: Qt Network does not support SSL/TLS!";
    qWarning() << "This may cause issues with HTTPS connections for update checks.";
    qWarning() << "The VPN tunnel itself is not affected (uses VEIL protocol).";
  }

  // List available TLS backends
  auto backends = QSslSocket::availableBackends();
  qDebug() << "Available TLS backends:" << backends;
  if (backends.isEmpty()) {
    qWarning() << "WARNING: No TLS backends available!";
    qWarning() << "Expected backends: 'schannel' (Windows native) or 'openssl'";
  }

  // Get active TLS backend
  QString activeBackend = QSslSocket::activeBackend();
  qDebug() << "Active TLS backend:" << (activeBackend.isEmpty() ? "none" : activeBackend);
#endif

  qDebug() << "===============================";

  // Set application metadata
  app.setOrganizationName("VEIL");
  app.setOrganizationDomain("veil.local");
  app.setApplicationName("VEIL Client");
  app.setApplicationVersion(veil::kVersionString);

  // Load translations
  QSettings settings("VEIL", "VPN Client");
  QString languageCode = settings.value("ui/language", "en").toString();

  // Auto-detect system language if not set or invalid
  QStringList supportedLanguages = {"en", "ru", "zh"};
  if (!supportedLanguages.contains(languageCode)) {
    // Try to use system locale
    QString systemLocale = QLocale::system().name();  // e.g., "en_US", "ru_RU", "zh_CN"
    QString systemLang = systemLocale.left(2);  // Get language code (first 2 chars)

    if (supportedLanguages.contains(systemLang)) {
      languageCode = systemLang;
      qDebug() << "Auto-detected system language:" << systemLang;
    } else {
      languageCode = "en";  // Default to English
      qDebug() << "System language not supported, defaulting to English";
    }
  }

  qDebug() << "Loading translations for language:" << languageCode;

  // English is the source language — no translation files are needed.
  // Only load translations for non-English languages.
  QTranslator qtTranslator;
  QTranslator appTranslator;

  if (languageCode != "en") {
    // Load Qt's built-in translations (for standard dialogs)
    if (qtTranslator.load("qt_" + languageCode, QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
      app.installTranslator(&qtTranslator);
      qDebug() << "Loaded Qt base translations for" << languageCode;
    } else {
      qDebug() << "Qt base translations not found for" << languageCode
               << "(standard dialogs will appear in English)";
    }

    // Load application translations
    QString translationsPath = QCoreApplication::applicationDirPath() + "/translations";
    QString translationFile = "veil_" + languageCode;

    qDebug() << "Looking for translation file:" << translationFile << "in" << translationsPath;

    if (appTranslator.load(translationFile, translationsPath)) {
      app.installTranslator(&appTranslator);
      qDebug() << "Successfully loaded application translations:" << translationFile;
    } else {
      // Try to load from resource path (for bundled translations)
      if (appTranslator.load(":/translations/" + translationFile)) {
        app.installTranslator(&appTranslator);
        qDebug() << "Successfully loaded application translations from resources:" << translationFile;
      } else {
        qWarning() << "Failed to load application translations for" << languageCode;
        qWarning() << "Tried paths:" << translationsPath << "and :/translations/";
        qWarning() << "UI will fall back to English";
      }
    }
  } else {
    qDebug() << "English is the source language, no translation files needed";
  }

#ifdef _WIN32
  // On Windows, check if we have admin rights. If not, request elevation.
  // Admin rights are needed to start/manage the VPN service.
  qDebug() << "Checking administrator privileges...";
  if (!veil::windows::elevation::is_elevated()) {
    qDebug() << "Not running as administrator, requesting elevation...";
    // Not elevated - request elevation and restart
    QMessageBox::information(
        nullptr,
        QObject::tr("Administrator Rights Required"),
        QObject::tr("VEIL VPN Client requires administrator privileges\n"
                    "to manage the VPN service.\n\n"
                    "The application will now request elevation."));

    // Request elevation - this will restart the app as admin
    if (veil::windows::elevation::request_elevation("")) {
      // Elevated process was started, exit this instance
      qDebug() << "Elevated process launched, exiting non-elevated instance";
      return 0;
    }

    // User declined or elevation failed
    qWarning() << "Elevation request failed or was declined by user";
    QMessageBox::critical(
        nullptr,
        QObject::tr("Elevation Failed"),
        QObject::tr("Administrator privileges are required to run VEIL VPN.\n\n"
                    "Please run the application as Administrator."));
    return 1;
  }
  qDebug() << "Running with administrator privileges";
#endif

  qDebug() << "Creating main window...";

  // Check for command-line arguments
  QStringList args = app.arguments();
  bool startMinimized = args.contains("--minimized") || args.contains("-m");

  try {
    // Create main window
    veil::gui::MainWindow window;

    qDebug() << "Main window created successfully";

    // Show window unless minimized flag is set
    if (!startMinimized) {
      window.show();
      qDebug() << "Main window shown";
    } else {
      qDebug() << "Starting minimized due to --minimized flag";
      // Window will be hidden by the startMinimized logic in MainWindow constructor
      window.show();  // Still call show() first, then hide() in constructor
    }

    qDebug() << "Entering application event loop";
    return app.exec();
  } catch (const std::exception& e) {
    qCritical() << "FATAL: Unhandled exception during startup:" << e.what();
#ifdef _WIN32
    // Keep console open so the user can read the error
    fprintf(stderr, "\nPress Enter to exit...\n");
    fflush(stderr);
    getchar();
#endif
    return 1;
  } catch (...) {
    qCritical() << "FATAL: Unknown exception during startup";
#ifdef _WIN32
    fprintf(stderr, "\nPress Enter to exit...\n");
    fflush(stderr);
    getchar();
#endif
    return 1;
  }
}
