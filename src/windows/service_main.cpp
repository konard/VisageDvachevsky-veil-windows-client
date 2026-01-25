#ifdef _WIN32

// Windows Service entry point for VEIL VPN daemon
// This executable runs as a Windows service and manages the VPN connection.
//
// NOTE: The full VPN tunnel functionality is not yet available on Windows.
// The transport layer (UDP socket, event loop) needs to be ported from Linux.
// Currently, this service provides the framework for:
// - Windows service installation/uninstallation
// - Service start/stop/status management
// - IPC communication with the GUI client
//
// When the transport layer is ported, the tunnel functionality can be enabled.

#include <windows.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <type_traits>

#include "../common/ipc/ipc_protocol.h"
#include "../common/ipc/ipc_socket.h"
#include "../common/logging/logger.h"
#include "service_manager.h"

using namespace veil;
using namespace veil::windows;

// Placeholder statistics until tunnel is available
struct ServiceStats {
  std::uint64_t bytes_sent{0};
  std::uint64_t bytes_received{0};
  std::uint64_t packets_sent{0};
  std::uint64_t packets_received{0};
};

// Global state
static std::atomic<bool> g_running{false};
static std::atomic<bool> g_connected{false};
static std::unique_ptr<ipc::IpcServer> g_ipc_server;
static ServiceStats g_stats;

// Forward declarations
void WINAPI service_main(DWORD argc, LPSTR* argv);
void run_service();
void stop_service();
void handle_ipc_message(const ipc::Message& msg, int client_fd);

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
  // Check if running with console arguments for debugging/installation
  if (argc > 1) {
    std::string arg = argv[1];

    if (arg == "--install" || arg == "-i") {
      // Install the service
      if (!elevation::is_elevated()) {
        std::cout << "Administrator privileges required. Requesting elevation..."
                  << std::endl;
        return elevation::request_elevation("--install") ? 0 : 1;
      }

      // Get the path to this executable
      char path[MAX_PATH];
      GetModuleFileNameA(nullptr, path, MAX_PATH);

      std::string error;
      if (ServiceManager::install(path, error)) {
        std::cout << "Service installed successfully." << std::endl;
        return 0;
      } else {
        std::cerr << "Failed to install service: " << error << std::endl;
        return 1;
      }
    }

    if (arg == "--uninstall" || arg == "-u") {
      // Uninstall the service
      if (!elevation::is_elevated()) {
        std::cout << "Administrator privileges required. Requesting elevation..."
                  << std::endl;
        return elevation::request_elevation("--uninstall") ? 0 : 1;
      }

      std::string error;
      if (ServiceManager::uninstall(error)) {
        std::cout << "Service uninstalled successfully." << std::endl;
        return 0;
      } else {
        std::cerr << "Failed to uninstall service: " << error << std::endl;
        return 1;
      }
    }

    if (arg == "--start" || arg == "-s") {
      // Start the service
      std::string error;
      if (ServiceManager::start(error)) {
        std::cout << "Service started." << std::endl;
        return 0;
      } else {
        std::cerr << "Failed to start service: " << error << std::endl;
        return 1;
      }
    }

    if (arg == "--stop" || arg == "-t") {
      // Stop the service
      std::string error;
      if (ServiceManager::stop(error)) {
        std::cout << "Service stopped." << std::endl;
        return 0;
      } else {
        std::cerr << "Failed to stop service: " << error << std::endl;
        return 1;
      }
    }

    if (arg == "--status") {
      // Query service status
      if (!ServiceManager::is_installed()) {
        std::cout << "Service is not installed." << std::endl;
        return 1;
      }
      std::cout << "Service status: " << ServiceManager::get_status_string()
                << std::endl;
      return 0;
    }

    if (arg == "--debug" || arg == "-d") {
      // Run in console mode for debugging
      std::cout << "Running in debug mode (press Ctrl+C to stop)..."
                << std::endl;

      // Initialize logging to console
      logging::configure_logging(logging::LogLevel::debug, true);

      // Set up signal handler for Ctrl+C
      signal(SIGINT, [](int) {
        std::cout << "\nStopping..." << std::endl;
        stop_service();
      });

      run_service();
      return 0;
    }

    if (arg == "--help" || arg == "-h") {
      std::cout << "VEIL VPN Service\n"
                << "\n"
                << "Usage: veil-service.exe [options]\n"
                << "\n"
                << "Options:\n"
                << "  --install, -i    Install the Windows service\n"
                << "  --uninstall, -u  Uninstall the Windows service\n"
                << "  --start, -s      Start the service\n"
                << "  --stop, -t       Stop the service\n"
                << "  --status         Query service status\n"
                << "  --debug, -d      Run in console mode for debugging\n"
                << "  --help, -h       Show this help message\n"
                << std::endl;
      return 0;
    }

    std::cerr << "Unknown argument: " << arg << std::endl;
    std::cerr << "Use --help for usage information." << std::endl;
    return 1;
  }

  // No arguments - run as Windows service
  SERVICE_TABLE_ENTRYA service_table[] = {
      {const_cast<char*>(ServiceManager::kServiceName), service_main},
      {nullptr, nullptr}};

  if (!StartServiceCtrlDispatcherA(service_table)) {
    DWORD err = GetLastError();
    if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
      // Not being run as a service - show help
      std::cerr << "This program is intended to run as a Windows service.\n"
                << "Use --help for command line options." << std::endl;
    } else {
      std::cerr << "Failed to start service control dispatcher: " << err
                << std::endl;
    }
    return 1;
  }

  return 0;
}

// ============================================================================
// Service Main Function
// ============================================================================

void WINAPI service_main(DWORD /*argc*/, LPSTR* /*argv*/) {
  // Register service control handler
  if (!ServiceControlHandler::init(ServiceManager::kServiceName)) {
    return;
  }

  // Report starting
  ServiceControlHandler::report_starting(1);

  // Initialize logging to Windows Event Log
  logging::configure_logging(logging::LogLevel::info, false);

  // Set stop handler
  ServiceControlHandler::on_stop([]() { stop_service(); });

  // Report starting progress
  ServiceControlHandler::report_starting(2);

  // Run the service
  run_service();

  // Report stopped
  ServiceControlHandler::report_stopped(NO_ERROR);
}

// ============================================================================
// Service Logic
// ============================================================================

void run_service() {
  g_running = true;
  g_connected = false;

  // Create IPC server for GUI communication
  g_ipc_server = std::make_unique<ipc::IpcServer>();
  g_ipc_server->on_message(handle_ipc_message);

  std::error_code ec;
  if (!g_ipc_server->start(ec)) {
    LOG_ERROR("Failed to start IPC server: {}", ec.message());
    // Continue anyway - service can still run
  } else {
    LOG_INFO("IPC server started successfully");
  }

  // Report that we're running
  ServiceControlHandler::report_running();

  LOG_INFO("VEIL VPN Service started");
  LOG_WARN("Note: VPN tunnel functionality is not yet available on Windows. "
           "The transport layer needs to be ported from Linux.");

  // Main service loop
  while (g_running) {
    // Poll IPC server for messages
    if (g_ipc_server) {
      std::error_code ipc_ec;
      g_ipc_server->poll(ipc_ec);
    }

    // Small sleep to prevent busy-waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Cleanup
  if (g_ipc_server) {
    g_ipc_server->stop();
    g_ipc_server.reset();
  }

  LOG_INFO("VEIL VPN Service stopped");
}

void stop_service() {
  g_running = false;
}

// ============================================================================
// IPC Message Handler
// ============================================================================

void handle_ipc_message(const ipc::Message& msg, int client_fd) {
  if (!std::holds_alternative<ipc::Command>(msg.payload)) {
    LOG_WARN("Received non-command message from client");
    return;
  }

  const auto& cmd = std::get<ipc::Command>(msg.payload);

  // NOTE: VPN tunnel functionality is not yet available on Windows.
  // These handlers provide the IPC framework but actual connection
  // will fail until the transport layer is ported.

  ipc::Response response;
  ipc::Message response_msg;
  response_msg.type = ipc::MessageType::kResponse;
  response_msg.id = msg.id;

  // Use std::visit to handle the Command variant
  std::visit(
      [&response, &response_msg](const auto& command) {
        using T = std::decay_t<decltype(command)>;

        if constexpr (std::is_same_v<T, ipc::ConnectCommand>) {
          LOG_DEBUG("Received ConnectCommand");
          if (g_connected) {
            response = ipc::ErrorResponse{"Already connected", ""};
          } else {
            // VPN tunnel not yet available on Windows
            const char* error_msg =
                "VPN tunnel functionality is not yet available on Windows. "
                "The transport layer (UDP/event loop) needs to be ported from "
                "Linux.";
            LOG_WARN("{}", error_msg);
            response = ipc::ErrorResponse{error_msg, ""};
          }
        } else if constexpr (std::is_same_v<T, ipc::DisconnectCommand>) {
          LOG_DEBUG("Received DisconnectCommand");
          if (g_connected) {
            g_connected = false;
            response = ipc::SuccessResponse{"Disconnected successfully"};

            // Broadcast disconnection event
            ipc::ConnectionStateChangeEvent event;
            event.old_state = ipc::ConnectionState::kConnected;
            event.new_state = ipc::ConnectionState::kDisconnected;
            event.message = "Disconnected";

            ipc::Message event_msg;
            event_msg.type = ipc::MessageType::kEvent;
            event_msg.payload = ipc::Event{event};
            g_ipc_server->broadcast_message(event_msg);
          } else {
            response = ipc::ErrorResponse{"Not connected", ""};
          }
        } else if constexpr (std::is_same_v<T, ipc::GetStatusCommand>) {
          LOG_DEBUG("Received GetStatusCommand");
          ipc::StatusResponse status_resp;
          status_resp.status.state = g_connected
                                         ? ipc::ConnectionState::kConnected
                                         : ipc::ConnectionState::kDisconnected;
          status_resp.status.error_message =
              "VPN tunnel not yet available on Windows. Service is running.";
          response = status_resp;
        } else if constexpr (std::is_same_v<T, ipc::GetMetricsCommand>) {
          LOG_DEBUG("Received GetMetricsCommand");
          ipc::MetricsResponse metrics_resp;
          metrics_resp.metrics.total_tx_bytes = g_stats.bytes_sent;
          metrics_resp.metrics.total_rx_bytes = g_stats.bytes_received;
          response = metrics_resp;
        } else if constexpr (std::is_same_v<T, ipc::GetDiagnosticsCommand>) {
          LOG_DEBUG("Received GetDiagnosticsCommand");
          ipc::DiagnosticsResponse diag_resp;
          diag_resp.diagnostics.protocol.packets_sent = g_stats.packets_sent;
          diag_resp.diagnostics.protocol.packets_received =
              g_stats.packets_received;
          response = diag_resp;
        } else if constexpr (std::is_same_v<T, ipc::UpdateConfigCommand>) {
          LOG_DEBUG("Received UpdateConfigCommand");
          // Configuration updates accepted but tunnel not functional
          response = ipc::SuccessResponse{
              "Configuration accepted (tunnel not yet available)"};
        } else if constexpr (std::is_same_v<T, ipc::ExportDiagnosticsCommand>) {
          LOG_DEBUG("Received ExportDiagnosticsCommand");
          response = ipc::ErrorResponse{
              "Export diagnostics not yet implemented on Windows", ""};
        } else if constexpr (std::is_same_v<T, ipc::GetClientListCommand>) {
          LOG_DEBUG("Received GetClientListCommand");
          ipc::ClientListResponse list_resp;
          // Empty client list - we're not a server
          response = list_resp;
        } else {
          LOG_WARN("Unknown command type received");
          response = ipc::ErrorResponse{"Unknown command", ""};
        }
      },
      cmd);

  response_msg.payload = response;

  std::error_code ec;
  g_ipc_server->send_message(client_fd, response_msg, ec);
}

#else
// Non-Windows builds - provide stub main
int main() {
  // This file is Windows-only
  return 1;
}
#endif  // _WIN32
