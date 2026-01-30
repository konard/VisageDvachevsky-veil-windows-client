#include <QApplication>
#include <QMessageBox>
#include <QDebug>

#ifdef QT_NETWORK_LIB
#include <QSslSocket>
#endif

#include "mainwindow.h"

#ifdef _WIN32
#include "windows/service_manager.h"
#endif

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  // Log Qt and SSL information for debugging
  qDebug() << "=== VEIL VPN Client Startup ===";
  qDebug() << "Qt Version:" << qVersion();
  qDebug() << "Application Version: 0.1.0";

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
  app.setApplicationVersion("0.1.0");

#ifdef _WIN32
  // On Windows, check if we have admin rights. If not, request elevation.
  // Admin rights are needed to start/manage the VPN service.
  if (!veil::windows::elevation::is_elevated()) {
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
      return 0;
    }

    // User declined or elevation failed
    QMessageBox::critical(
        nullptr,
        QObject::tr("Elevation Failed"),
        QObject::tr("Administrator privileges are required to run VEIL VPN.\n\n"
                    "Please run the application as Administrator."));
    return 1;
  }
#endif

  // Check for command-line arguments
  QStringList args = app.arguments();
  bool startMinimized = args.contains("--minimized") || args.contains("-m");

  // Create main window
  veil::gui::MainWindow window;

  // Show window unless minimized flag is set
  if (!startMinimized) {
    window.show();
  } else {
    qDebug() << "Starting minimized due to --minimized flag";
    // Window will be hidden by the startMinimized logic in MainWindow constructor
    window.show();  // Still call show() first, then hide() in constructor
  }

  return app.exec();
}
