#include <QApplication>
#include <QMessageBox>

#include "mainwindow.h"

#ifdef _WIN32
#include "windows/service_manager.h"
#endif

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

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

  // Create and show main window
  veil::gui::MainWindow window;
  window.show();

  return app.exec();
}
