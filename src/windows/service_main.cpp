#ifdef _WIN32

// Windows Service entry point for VEIL VPN daemon
// This executable runs as a Windows service and manages the VPN connection.
//
// The VPN tunnel functionality is now available on Windows with:
// - Windows service installation/uninstallation
// - Service start/stop/status management
// - IPC communication with the GUI client
// - Full VPN tunnel support using ported Windows event loop and UDP socket
// - Console control handler for graceful shutdown

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
#include "../tunnel/tunnel.h"
#include "console_handler.h"
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
static std::unique_ptr<tunnel::Tunnel> g_tunnel;
static std::unique_ptr<std::thread> g_tunnel_thread;
static tunnel::TunnelConfig g_tunnel_config;

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
      // Check for administrator privileges
      if (!elevation::is_elevated()) {
        std::cerr << "========================================" << std::endl;
        std::cerr << "ERROR: Administrator privileges required" << std::endl;
        std::cerr << "========================================" << std::endl;
        std::cerr << "\nThe VEIL VPN service requires administrator privileges to:" << std::endl;
        std::cerr << "  - Create virtual network adapters (Wintun)" << std::endl;
        std::cerr << "  - Configure IP addresses and routing" << std::endl;
        std::cerr << "  - Manage network interfaces" << std::endl;
        std::cerr << "\nPlease run this command from an elevated PowerShell or Command Prompt:" << std::endl;
        std::cerr << "  1. Right-click PowerShell/Command Prompt" << std::endl;
        std::cerr << "  2. Select 'Run as administrator'" << std::endl;
        std::cerr << "  3. Run the command again" << std::endl;
        std::cerr << "\nAlternatively, use this command to automatically elevate:" << std::endl;
        std::cerr << "  Start-Process -Verb RunAs -FilePath \"" << argv[0] << "\" -ArgumentList \"--debug\"" << std::endl;
        std::cerr << "========================================" << std::endl;
        return 1;
      }

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

  LOG_INFO("========================================");
  LOG_INFO("VEIL VPN SERVICE STARTING");
  LOG_INFO("========================================");

  // Create IPC server for GUI communication
  LOG_DEBUG("Creating IPC server instance...");
  g_ipc_server = std::make_unique<ipc::IpcServer>();
  g_ipc_server->on_message(handle_ipc_message);

  LOG_DEBUG("Starting IPC server...");
  std::error_code ec;
  if (!g_ipc_server->start(ec)) {
    LOG_ERROR("========================================");
    LOG_ERROR("IPC SERVER START FAILED");
    LOG_ERROR("========================================");
    LOG_ERROR("Error code: {}", ec.value());
    LOG_ERROR("Error message: {}", ec.message());
    LOG_ERROR("========================================");
    LOG_WARN("Service will continue but GUI will not be able to connect");
  } else {
    LOG_INFO("IPC server started successfully");
    LOG_INFO("Listening on named pipe: \\\\.\\pipe\\veil-client");
  }

  // Report that we're running
  ServiceControlHandler::report_running();

  LOG_INFO("========================================");
  LOG_INFO("VEIL VPN SERVICE RUNNING");
  LOG_INFO("========================================");
  LOG_INFO("Service is now accepting IPC connections");
  LOG_INFO("VPN tunnel functionality is available");

  // Main service loop
  LOG_DEBUG("Entering main service loop");
  std::chrono::steady_clock::time_point last_status_log = std::chrono::steady_clock::now();

  while (g_running) {
    // Poll IPC server for messages
    if (g_ipc_server) {
      std::error_code ipc_ec;
      g_ipc_server->poll(ipc_ec);
      if (ipc_ec && ipc_ec.value() != 0) {
        // Only log actual errors, not "would block" conditions
        LOG_DEBUG("IPC poll error (may be normal): {}", ipc_ec.message());
      }
    }

    // Periodic status logging (every 60 seconds)
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_status_log).count() >= 60) {
      LOG_DEBUG("Service status: running={}, connected={}", g_running.load(), g_connected.load());
      if (g_tunnel) {
        const auto& stats = g_tunnel->stats();
        LOG_DEBUG("Tunnel stats: TX={} bytes, RX={} bytes", stats.udp_bytes_sent, stats.udp_bytes_received);
      }
      last_status_log = now;
    }

    // Small sleep to prevent busy-waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  LOG_INFO("Exiting main service loop");

  // Cleanup
  LOG_INFO("========================================");
  LOG_INFO("SERVICE SHUTDOWN - CLEANUP STARTING");
  LOG_INFO("========================================");

  // Stop tunnel if running
  if (g_tunnel) {
    LOG_INFO("Stopping VPN tunnel...");
    g_tunnel->stop();
    if (g_tunnel_thread && g_tunnel_thread->joinable()) {
      LOG_DEBUG("Waiting for tunnel thread to terminate...");
      g_tunnel_thread->join();
      LOG_DEBUG("Tunnel thread terminated");
    }
    g_tunnel.reset();
    g_tunnel_thread.reset();
    LOG_INFO("VPN tunnel stopped and cleaned up");
  }

  if (g_ipc_server) {
    LOG_DEBUG("Stopping IPC server...");
    g_ipc_server->stop();
    g_ipc_server.reset();
    LOG_INFO("IPC server stopped");
  }

  LOG_INFO("========================================");
  LOG_INFO("VEIL VPN SERVICE STOPPED");
  LOG_INFO("========================================");
}

void stop_service() {
  g_running = false;

  // Also stop the tunnel if it's running
  if (g_tunnel) {
    g_tunnel->stop();
  }
}

// ============================================================================
// IPC Message Handler
// ============================================================================

void handle_ipc_message(const ipc::Message& msg, int client_fd) {
  LOG_DEBUG("========================================");
  LOG_DEBUG("IPC MESSAGE RECEIVED");
  LOG_DEBUG("========================================");
  LOG_DEBUG("Message type: {}", static_cast<int>(msg.type));
  if (msg.id) {
    LOG_DEBUG("Message ID: {}", *msg.id);
  }

  if (!std::holds_alternative<ipc::Command>(msg.payload)) {
    LOG_WARN("Received non-command message from client");
    LOG_WARN("Payload holds_alternative<Command>: {}", std::holds_alternative<ipc::Command>(msg.payload));
    LOG_WARN("Payload holds_alternative<Event>: {}", std::holds_alternative<ipc::Event>(msg.payload));
    LOG_WARN("Payload holds_alternative<Response>: {}", std::holds_alternative<ipc::Response>(msg.payload));
    return;
  }

  const auto& cmd = std::get<ipc::Command>(msg.payload);
  LOG_DEBUG("Successfully extracted Command from payload");

  ipc::Message response_msg;
  response_msg.type = ipc::MessageType::kResponse;
  response_msg.id = msg.id;

  // Use std::visit to handle the Command variant
  std::visit(
      [&response_msg](const auto& command) {
        ipc::Response response;
        using T = std::decay_t<decltype(command)>;

        if constexpr (std::is_same_v<T, ipc::ConnectCommand>) {
          LOG_DEBUG("========================================");
          LOG_DEBUG("PROCESSING CONNECT COMMAND");
          LOG_DEBUG("========================================");
          LOG_DEBUG("Server: {}:{}", command.config.server_address, command.config.server_port);
          LOG_DEBUG("Key file: {}", command.config.key_file);
          LOG_DEBUG("Obfuscation seed file: {}", command.config.obfuscation_seed_file);
          LOG_DEBUG("TUN device: {}", command.config.tun_device_name);
          LOG_DEBUG("TUN IP: {}", command.config.tun_ip_address);
          LOG_DEBUG("TUN netmask: {}", command.config.tun_netmask);
          LOG_DEBUG("TUN MTU: {}", command.config.tun_mtu);
          LOG_DEBUG("Enable obfuscation: {}", command.config.enable_obfuscation);
          LOG_DEBUG("Auto reconnect: {}", command.config.auto_reconnect);
          LOG_DEBUG("Route all traffic: {}", command.config.route_all_traffic);

          if (g_connected) {
            LOG_WARN("Already connected - rejecting connection request");
            response = ipc::ErrorResponse{"Already connected", ""};
          } else {
            LOG_INFO("Initializing new VPN connection...");
            // Configure tunnel from IPC command
            g_tunnel_config = tunnel::TunnelConfig{};
            g_tunnel_config.server_address = command.config.server_address;
            g_tunnel_config.server_port = command.config.server_port;
            g_tunnel_config.auto_reconnect = command.config.auto_reconnect;
            g_tunnel_config.reconnect_delay =
                std::chrono::seconds(command.config.reconnect_interval_sec);
            g_tunnel_config.max_reconnect_attempts =
                static_cast<int>(command.config.max_reconnect_attempts);

            // Cryptographic configuration - critical for VPN handshake!
            g_tunnel_config.key_file = command.config.key_file;
            g_tunnel_config.obfuscation_seed_file = command.config.obfuscation_seed_file;

            // Set TUN device configuration from IPC command (with defaults)
            g_tunnel_config.tun.device_name = command.config.tun_device_name.empty()
                ? "veil0" : command.config.tun_device_name;
            g_tunnel_config.tun.ip_address = command.config.tun_ip_address.empty()
                ? "10.8.0.2" : command.config.tun_ip_address;
            g_tunnel_config.tun.netmask = command.config.tun_netmask.empty()
                ? "255.255.255.0" : command.config.tun_netmask;
            g_tunnel_config.tun.mtu = command.config.tun_mtu > 0
                ? command.config.tun_mtu : 1400;

            // Log configuration for debugging
            LOG_INFO("Connecting to {}:{}", g_tunnel_config.server_address, g_tunnel_config.server_port);
            if (!g_tunnel_config.key_file.empty()) {
              LOG_DEBUG("Using pre-shared key file: {}", g_tunnel_config.key_file);
            } else {
              LOG_WARN("No pre-shared key file specified - handshake will fail!");
            }
            if (!g_tunnel_config.obfuscation_seed_file.empty()) {
              LOG_DEBUG("Using obfuscation seed file: {}", g_tunnel_config.obfuscation_seed_file);
            }

            // Create and initialize tunnel
            LOG_DEBUG("Creating tunnel instance with configuration...");
            g_tunnel = std::make_unique<tunnel::Tunnel>(g_tunnel_config);

            LOG_DEBUG("Initializing tunnel...");
            std::error_code ec;
            if (!g_tunnel->initialize(ec)) {
              LOG_ERROR("========================================");
              LOG_ERROR("TUNNEL INITIALIZATION FAILED");
              LOG_ERROR("========================================");
              LOG_ERROR("Error code: {}", ec.value());
              LOG_ERROR("Error message: {}", ec.message());
              LOG_ERROR("========================================");
              response = ipc::ErrorResponse{"Failed to initialize tunnel: " + ec.message(), ""};
              g_tunnel.reset();
            } else {
              LOG_INFO("Tunnel initialized successfully");
              LOG_DEBUG("Starting tunnel thread...");

              // Start tunnel in background thread
              g_tunnel_thread = std::make_unique<std::thread>([&]() {
                LOG_INFO("========================================");
                LOG_INFO("VPN TUNNEL THREAD STARTED");
                LOG_INFO("========================================");
                LOG_INFO("Running tunnel event loop...");
                g_tunnel->run();
                LOG_INFO("========================================");
                LOG_INFO("VPN TUNNEL STOPPED");
                LOG_INFO("========================================");
                g_connected = false;

                // Broadcast disconnection event
                ipc::ConnectionStateChangeEvent event;
                event.old_state = ipc::ConnectionState::kConnected;
                event.new_state = ipc::ConnectionState::kDisconnected;
                event.message = "Tunnel stopped";

                ipc::Message event_msg;
                event_msg.type = ipc::MessageType::kEvent;
                event_msg.payload = ipc::Event{event};
                if (g_ipc_server) {
                  g_ipc_server->broadcast_message(event_msg);
                }
              });

              g_connected = true;

              LOG_INFO("========================================");
              LOG_INFO("VPN CONNECTION ESTABLISHED");
              LOG_INFO("========================================");
              LOG_DEBUG("Setting response to SuccessResponse");
              response = ipc::SuccessResponse{"Connected successfully"};

              // Broadcast connection event
              LOG_DEBUG("Broadcasting connection state change event...");
              ipc::ConnectionStateChangeEvent event;
              event.old_state = ipc::ConnectionState::kDisconnected;
              event.new_state = ipc::ConnectionState::kConnected;
              event.message = "Connected to " + command.config.server_address;

              ipc::Message event_msg;
              event_msg.type = ipc::MessageType::kEvent;
              event_msg.payload = ipc::Event{event};
              g_ipc_server->broadcast_message(event_msg);
              LOG_DEBUG("Connection state change event broadcasted");
            }
          }
        } else if constexpr (std::is_same_v<T, ipc::DisconnectCommand>) {
          LOG_DEBUG("Received DisconnectCommand");
          if (g_connected && g_tunnel) {
            LOG_INFO("Stopping VPN tunnel...");
            g_tunnel->stop();
            if (g_tunnel_thread && g_tunnel_thread->joinable()) {
              g_tunnel_thread->join();
            }
            g_tunnel.reset();
            g_tunnel_thread.reset();
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
          if (g_tunnel) {
            status_resp.status.server_address = g_tunnel_config.server_address;
            status_resp.status.server_port = g_tunnel_config.server_port;
            const auto& stats = g_tunnel->stats();
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - stats.connected_since);
            status_resp.status.uptime_sec = static_cast<std::uint64_t>(uptime.count());
          }
          response = status_resp;
        } else if constexpr (std::is_same_v<T, ipc::GetMetricsCommand>) {
          LOG_DEBUG("Received GetMetricsCommand");
          ipc::MetricsResponse metrics_resp;
          if (g_tunnel) {
            const auto& stats = g_tunnel->stats();
            metrics_resp.metrics.total_tx_bytes = stats.udp_bytes_sent;
            metrics_resp.metrics.total_rx_bytes = stats.udp_bytes_received;
          } else {
            metrics_resp.metrics.total_tx_bytes = g_stats.bytes_sent;
            metrics_resp.metrics.total_rx_bytes = g_stats.bytes_received;
          }
          response = metrics_resp;
        } else if constexpr (std::is_same_v<T, ipc::GetDiagnosticsCommand>) {
          LOG_DEBUG("Received GetDiagnosticsCommand");
          ipc::DiagnosticsResponse diag_resp;
          if (g_tunnel) {
            const auto& stats = g_tunnel->stats();
            diag_resp.diagnostics.protocol.packets_sent = stats.udp_packets_sent;
            diag_resp.diagnostics.protocol.packets_received = stats.udp_packets_received;
          } else {
            diag_resp.diagnostics.protocol.packets_sent = g_stats.packets_sent;
            diag_resp.diagnostics.protocol.packets_received = g_stats.packets_received;
          }
          response = diag_resp;
        } else if constexpr (std::is_same_v<T, ipc::UpdateConfigCommand>) {
          LOG_DEBUG("Received UpdateConfigCommand");
          // Store config for next connection
          g_tunnel_config.server_address = command.config.server_address;
          g_tunnel_config.server_port = command.config.server_port;
          g_tunnel_config.auto_reconnect = command.config.auto_reconnect;
          g_tunnel_config.reconnect_delay =
              std::chrono::seconds(command.config.reconnect_interval_sec);
          g_tunnel_config.max_reconnect_attempts =
              static_cast<int>(command.config.max_reconnect_attempts);
          // Cryptographic configuration
          g_tunnel_config.key_file = command.config.key_file;
          g_tunnel_config.obfuscation_seed_file = command.config.obfuscation_seed_file;
          // TUN configuration
          if (!command.config.tun_device_name.empty()) {
            g_tunnel_config.tun.device_name = command.config.tun_device_name;
          }
          if (!command.config.tun_ip_address.empty()) {
            g_tunnel_config.tun.ip_address = command.config.tun_ip_address;
          }
          if (!command.config.tun_netmask.empty()) {
            g_tunnel_config.tun.netmask = command.config.tun_netmask;
          }
          if (command.config.tun_mtu > 0) {
            g_tunnel_config.tun.mtu = command.config.tun_mtu;
          }
          response = ipc::SuccessResponse{"Configuration updated"};
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

        // Set the response payload inside the visitor to ensure it's always set
        response_msg.payload = response;
      },
      cmd);

  // Log response details before sending
  LOG_DEBUG("========================================");
  LOG_DEBUG("SENDING IPC RESPONSE");
  LOG_DEBUG("========================================");
  LOG_DEBUG("Response message type: {}", static_cast<int>(response_msg.type));
  if (response_msg.id) {
    LOG_DEBUG("Response message ID: {}", *response_msg.id);
  }

  // Log which response type we're sending by checking response_msg.payload
  if (!std::holds_alternative<ipc::Response>(response_msg.payload)) {
    LOG_ERROR("Response payload is not a Response variant!");
    LOG_ERROR("Payload index: {}", response_msg.payload.index());
  } else {
    const auto& response = std::get<ipc::Response>(response_msg.payload);
    if (std::holds_alternative<ipc::SuccessResponse>(response)) {
      const auto& sr = std::get<ipc::SuccessResponse>(response);
      LOG_DEBUG("Response type: SuccessResponse");
      LOG_DEBUG("  Message: {}", sr.message);
    } else if (std::holds_alternative<ipc::ErrorResponse>(response)) {
      const auto& er = std::get<ipc::ErrorResponse>(response);
      LOG_DEBUG("Response type: ErrorResponse");
      LOG_DEBUG("  Error: {}", er.error_message);
      LOG_DEBUG("  Details: {}", er.details);
    } else if (std::holds_alternative<ipc::StatusResponse>(response)) {
      const auto& sr = std::get<ipc::StatusResponse>(response);
      LOG_DEBUG("Response type: StatusResponse");
      LOG_DEBUG("  State: {}", static_cast<int>(sr.status.state));
    } else if (std::holds_alternative<ipc::MetricsResponse>(response)) {
      LOG_DEBUG("Response type: MetricsResponse");
    } else if (std::holds_alternative<ipc::DiagnosticsResponse>(response)) {
      LOG_DEBUG("Response type: DiagnosticsResponse");
    } else if (std::holds_alternative<ipc::ClientListResponse>(response)) {
      LOG_DEBUG("Response type: ClientListResponse");
    } else {
      LOG_WARN("Response type: UNKNOWN!");
    }
  }

  std::error_code ec;
  if (!g_ipc_server->send_message(client_fd, response_msg, ec)) {
    LOG_ERROR("Failed to send IPC response: {}", ec.message());
  } else {
    LOG_DEBUG("IPC response sent successfully");
  }
  LOG_DEBUG("========================================");
}

#else
// Non-Windows builds - provide stub main
int main() {
  // This file is Windows-only
  return 1;
}
#endif  // _WIN32
