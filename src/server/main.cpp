#include <arpa/inet.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>

#include "common/cli/cli_utils.h"
#include "common/crypto/crypto_engine.h"
#include "common/daemon/daemon.h"
#include "common/handshake/handshake_processor.h"
#include "common/logging/logger.h"
#include "common/signal/signal_handler.h"
#include "common/utils/rate_limiter.h"
#include "server/server_config.h"
#include "server/session_table.h"
#include "transport/mux/frame.h"
#include "transport/session/transport_session.h"
#include "transport/udp_socket/udp_socket.h"
#include "tun/routing.h"
#include "tun/tun_device.h"

using namespace veil;

namespace {
constexpr std::size_t kMaxPacketSize = 65535;

// Minimum expected packet size for both data and handshake packets.
// This is the absolute minimum to filter out obviously malformed packets
// before any cryptographic processing. Actual validation happens in the
// handshake processor and transport session.
// Value: nonce (12 bytes) + min ciphertext (1 byte) + AEAD tag (16 bytes) = 29 bytes
constexpr std::size_t kMinPacketSize = 29;

// Statistics for display
struct ServerStats {
  std::atomic<uint64_t> total_bytes_sent{0};
  std::atomic<uint64_t> total_bytes_received{0};
  std::atomic<uint64_t> total_packets_sent{0};
  std::atomic<uint64_t> total_packets_received{0};
  std::atomic<uint64_t> connections_total{0};
  std::atomic<uint64_t> connections_active{0};
  std::chrono::steady_clock::time_point start_time;
};

ServerStats g_stats;

bool load_key_from_file(const std::string& path, std::array<std::uint8_t, 32>& key,
                        std::error_code& ec) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    ec = std::error_code(errno, std::generic_category());
    return false;
  }
  file.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
  if (file.gcount() != static_cast<std::streamsize>(key.size())) {
    ec = std::make_error_code(std::errc::io_error);
    return false;
  }
  return true;
}

// Helper to provide actionable error message for key file issues.
std::string format_key_error(const std::string& key_type, const std::string& path,
                             const std::error_code& ec) {
  std::string msg = key_type + " file '" + path + "' error: " + ec.message();
  if (ec == std::errc::no_such_file_or_directory) {
    msg += "\n  To generate a new key, run:\n";
    msg += "    head -c 32 /dev/urandom > " + path + "\n";
    msg += "  Then copy this file securely to both server and client.";
  } else if (ec == std::errc::permission_denied) {
    msg += "\n  Check file permissions with: ls -la " + path + "\n";
    msg += "  Ensure the file is readable by the current user.";
  } else if (ec == std::errc::io_error) {
    msg += "\n  The key file must be exactly 32 bytes.\n";
    msg += "  Regenerate with: head -c 32 /dev/urandom > " + path;
  }
  return msg;
}

// Helper functions for logging
void log_signal_sigint() {
  LOG_INFO("Received SIGINT, shutting down...");
  std::cout << '\n';
  cli::print_warning("Received interrupt signal, initiating graceful shutdown...");
}

void log_signal_sigterm() {
  LOG_INFO("Received SIGTERM, shutting down...");
  std::cout << '\n';
  cli::print_warning("Received termination signal, initiating graceful shutdown...");
}

void log_tun_write_error(const std::error_code& ec) {
  LOG_ERROR("Failed to write to TUN: {}", ec.message());
}

void log_handshake_send_error(const std::error_code& ec) {
  LOG_ERROR("Failed to send handshake response: {}", ec.message());
}

void log_retransmit_error(const std::error_code& ec) {
  LOG_WARN("Failed to retransmit to client: {}", ec.message());
}

void log_new_client(const std::string& host, std::uint16_t port, std::uint64_t session_id) {
  LOG_INFO("New client connected from {}:{}, session {}", host, port, session_id);

  g_stats.connections_total++;
  g_stats.connections_active++;

  auto& state = cli::cli_state();
  if (state.use_color) {
    std::cout << cli::colors::kBrightGreen << cli::symbols::kCircle << cli::colors::kReset
              << " Client connected: " << cli::colors::kBrightCyan << host << ":" << port
              << cli::colors::kReset << " (session " << cli::colors::kDim << session_id
              << cli::colors::kReset << ")" << '\n';
  } else {
    std::cout << "[+] Client connected: " << host << ":" << port << " (session " << session_id
              << ")" << '\n';
  }
}

[[maybe_unused]]
void log_client_disconnected(const std::string& host, std::uint16_t port,
                              std::uint64_t session_id) {
  LOG_INFO("Client disconnected: {}:{}, session {}", host, port, session_id);

  if (g_stats.connections_active > 0) {
    g_stats.connections_active--;
  }

  auto& state = cli::cli_state();
  if (state.use_color) {
    std::cout << cli::colors::kBrightRed << cli::symbols::kCircleEmpty << cli::colors::kReset
              << " Client disconnected: " << cli::colors::kDim << host << ":" << port
              << cli::colors::kReset << " (session " << cli::colors::kDim << session_id
              << cli::colors::kReset << ")" << '\n';
  } else {
    std::cout << "[-] Client disconnected: " << host << ":" << port << " (session " << session_id
              << ")" << '\n';
  }
}

void log_packet_received([[maybe_unused]] std::size_t size,
                         [[maybe_unused]] const std::string& host,
                         [[maybe_unused]] std::uint16_t port) {
  LOG_DEBUG("Received {} bytes from {}:{}", size, host, port);
  g_stats.total_packets_received++;
  g_stats.total_bytes_received += size;
}

void print_configuration(const server::ServerConfig& config) {
  cli::print_section("Server Configuration");
  cli::print_row("Listen Address", config.listen_address + ":" + std::to_string(config.listen_port));
  cli::print_row("Max Clients", std::to_string(config.max_clients));
  cli::print_row("Session Timeout", std::to_string(config.session_timeout.count()) + "s");
  cli::print_row("TUN Device", config.tunnel.tun.device_name);
  cli::print_row("TUN IP", config.tunnel.tun.ip_address);
  cli::print_row("IP Pool", config.ip_pool_start + " - " + config.ip_pool_end);
  cli::print_row("NAT Enabled", config.nat.enable_forwarding ? "Yes" : "No");
  if (config.nat.enable_forwarding) {
    cli::print_row("External Interface", config.nat.external_interface);
  }
  cli::print_row("Verbose", config.verbose ? "Yes" : "No");
  cli::print_row("Daemon Mode", config.daemon_mode ? "Yes" : "No");
  std::cout << '\n';
}

void print_server_status(std::size_t max_clients) {
  auto now = std::chrono::steady_clock::now();
  auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - g_stats.start_time).count();

  cli::print_section("Server Status");
  cli::print_row_colored("Status", "Running", cli::colors::kBrightGreen);
  cli::print_row("Uptime", cli::format_duration(uptime_seconds));
  cli::print_row("Active Clients", std::to_string(g_stats.connections_active.load()) + "/" +
                                       std::to_string(max_clients));
  cli::print_row("Total Connections", std::to_string(g_stats.connections_total.load()));
  cli::print_row("Bytes Sent", cli::format_bytes(g_stats.total_bytes_sent.load()));
  cli::print_row("Bytes Received", cli::format_bytes(g_stats.total_bytes_received.load()));
  cli::print_row("Packets Sent", std::to_string(g_stats.total_packets_sent.load()));
  cli::print_row("Packets Received", std::to_string(g_stats.total_packets_received.load()));
  std::cout << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
  // Parse configuration
  server::ServerConfig config;
  std::error_code ec;

  if (!server::parse_args(argc, argv, config, ec)) {
    cli::print_error("Failed to parse arguments: " + ec.message());
    std::cerr << '\n';
    std::cerr << "Usage: veil-server [-p <port>] [options]" << '\n';
    std::cerr << '\n';
    std::cerr << "Options:" << '\n';
    std::cerr << "  -p, --port <port>        Listen port (default: 4433)" << '\n';
    std::cerr << "  -l, --listen <addr>      Listen address (default: 0.0.0.0)" << '\n';
    std::cerr << "  -c, --config <file>      Configuration file path" << '\n';
    std::cerr << "  -k, --key <file>         Pre-shared key file" << '\n';
    std::cerr << "  -m, --max-clients <n>    Maximum clients (default: 256)" << '\n';
    std::cerr << "  -d, --daemon             Run as daemon" << '\n';
    std::cerr << "  -v, --verbose            Enable verbose logging" << '\n';
    std::cerr << "  --tun-name <name>        TUN device name (default: veil0)" << '\n';
    std::cerr << "  --tun-ip <ip>            TUN device IP (default: 10.8.0.1)" << '\n';
    std::cerr << "  --nat                    Enable NAT forwarding" << '\n';
    std::cerr << "  --nat-interface <iface>  External NAT interface" << '\n';
    std::cerr << '\n';
    return EXIT_FAILURE;
  }

  // Finalize configuration (auto-detect interfaces, etc.)
  if (!server::finalize_config(config, ec)) {
    cli::print_error("Configuration finalization failed: " + ec.message());
    return EXIT_FAILURE;
  }

  // Validate configuration
  std::string validation_error;
  if (!server::validate_config(config, validation_error)) {
    cli::print_error("Configuration error: " + validation_error);
    return EXIT_FAILURE;
  }

  // Print banner (only if not daemon mode)
  if (!config.daemon_mode) {
    cli::print_banner("VEIL VPN Server", "1.0.0");
    print_configuration(config);
  }

  // Initialize logging
  logging::configure_logging(config.verbose ? logging::LogLevel::debug : logging::LogLevel::info,
                              true);
  LOG_INFO("VEIL Server starting...");

  // Check if already running
  if (!config.pid_file.empty() && daemon::is_already_running(config.pid_file, ec)) {
    cli::print_error("Another instance is already running (PID file: " + config.pid_file + ")");
    return EXIT_FAILURE;
  }

  // Daemonize if requested
  if (config.daemon_mode) {
    daemon::DaemonConfig daemon_config;
    daemon_config.pid_file = config.pid_file;
    daemon_config.user = config.user;
    daemon_config.group = config.group;

    cli::print_info("Daemonizing...");
    LOG_INFO("Daemonizing...");
    if (!daemon::daemonize(daemon_config, ec)) {
      cli::print_error("Failed to daemonize: " + ec.message());
      LOG_ERROR("Failed to daemonize: {}", ec.message());
      return EXIT_FAILURE;
    }
  }

  // Create PID file if not daemonizing
  std::unique_ptr<daemon::PidFile> pid_file;
  if (!config.daemon_mode && !config.pid_file.empty()) {
    pid_file = std::make_unique<daemon::PidFile>(config.pid_file);
    if (!pid_file->create(ec)) {
      cli::print_warning("Failed to create PID file: " + ec.message());
      LOG_WARN("Failed to create PID file: {}", ec.message());
    }
  }

  // Load keys
  [[maybe_unused]] crypto::KeyPair key_pair = crypto::generate_x25519_keypair();
  std::vector<std::uint8_t> psk;
  if (!config.tunnel.key_file.empty()) {
    std::array<std::uint8_t, 32> psk_arr{};
    if (load_key_from_file(config.tunnel.key_file, psk_arr, ec)) {
      psk.assign(psk_arr.begin(), psk_arr.end());
      cli::print_success("Pre-shared key loaded");
    } else {
      std::string error_msg = format_key_error("Pre-shared key", config.tunnel.key_file, ec);
      cli::print_error(error_msg);
      LOG_ERROR("{}", error_msg);
      return EXIT_FAILURE;
    }
  }

  // Open TUN device
  cli::print_info("Opening TUN device...");
  tun::TunDevice tun_device;
  if (!tun_device.open(config.tunnel.tun, ec)) {
    cli::print_error("Failed to open TUN device: " + ec.message());
    LOG_ERROR("Failed to open TUN device: {}", ec.message());
    return EXIT_FAILURE;
  }
  cli::print_success("TUN device " + tun_device.device_name() + " opened with IP " +
                     config.tunnel.tun.ip_address);
  LOG_INFO("TUN device {} opened with IP {}", tun_device.device_name(),
           config.tunnel.tun.ip_address);

  // Setup routing and NAT
  tun::RouteManager route_manager;

  if (config.nat.enable_forwarding) {
    cli::print_info("Configuring NAT...");
    config.nat.internal_interface = tun_device.device_name();
    if (!route_manager.configure_nat(config.nat, ec)) {
      cli::print_error("Failed to configure NAT: " + ec.message());
      LOG_ERROR("Failed to configure NAT: {}", ec.message());
      return EXIT_FAILURE;
    }
    cli::print_success("NAT configured: " + config.nat.internal_interface + " -> " +
                       config.nat.external_interface);
    LOG_INFO("NAT configured: {} -> {}", config.nat.internal_interface,
             config.nat.external_interface);
  }

  // Open UDP socket
  cli::print_info("Opening UDP socket...");
  transport::UdpSocket udp_socket;
  if (!udp_socket.open(config.listen_port, true, ec)) {
    cli::print_error("Failed to open UDP socket: " + ec.message());
    LOG_ERROR("Failed to open UDP socket: {}", ec.message());
    return EXIT_FAILURE;
  }
  cli::print_success("Listening on " + config.listen_address + ":" +
                     std::to_string(config.listen_port));
  LOG_INFO("Listening on {}:{}", config.listen_address, config.listen_port);

  // Create session table
  server::SessionTable session_table(config.max_clients, config.session_timeout,
                                      config.ip_pool_start, config.ip_pool_end);

  // Create handshake responder
  utils::TokenBucket rate_limiter(100.0, std::chrono::milliseconds(10));  // 100 tokens, 10ms refill
  handshake::HandshakeResponder responder(psk, config.tunnel.handshake_skew_tolerance, rate_limiter);

  // Setup signal handlers
  auto& sig_handler = signal::SignalHandler::instance();
  sig_handler.setup_defaults();

  std::atomic<bool> running{true};
  sig_handler.on(signal::Signal::kInterrupt, [&running](signal::Signal) {
    log_signal_sigint();
    running.store(false);
  });
  sig_handler.on(signal::Signal::kTerminate, [&running](signal::Signal) {
    log_signal_sigterm();
    running.store(false);
  });

  // Session cleanup timer
  auto last_cleanup = std::chrono::steady_clock::now();
  auto last_stats = std::chrono::steady_clock::now();

  // Record start time
  g_stats.start_time = std::chrono::steady_clock::now();

  // Print running status
  std::cout << '\n';
  cli::print_section("Server Running");

  auto& cli_st = cli::cli_state();
  if (cli_st.use_color) {
    std::cout << cli::colors::kBrightGreen << cli::symbols::kCircle << cli::colors::kReset
              << " Server is ready and accepting connections" << '\n';
    std::cout << cli::colors::kDim << "  Press Ctrl+C to stop" << cli::colors::kReset << '\n';
  } else {
    std::cout << "[*] Server is ready and accepting connections" << '\n';
    std::cout << "    Press Ctrl+C to stop" << '\n';
  }
  std::cout << '\n';

  LOG_INFO("Server running, accepting connections...");

  // Main server loop
  std::array<std::uint8_t, kMaxPacketSize> buffer{};

  while (running.load() && !sig_handler.should_terminate()) {
    // Poll UDP socket
    udp_socket.poll(
        [&](const transport::UdpPacket& pkt) {
          // Early rejection of obviously malformed packets (DoS prevention).
          // This filters out undersized packets before any crypto processing.
          if (pkt.data.size() < kMinPacketSize || pkt.data.size() > kMaxPacketSize) {
            LOG_DEBUG("Dropping packet with invalid size {} from {}:{}",
                      pkt.data.size(), pkt.remote.host, pkt.remote.port);
            return;
          }

          log_packet_received(pkt.data.size(), pkt.remote.host, pkt.remote.port);

          // Check if this is from an existing session
          auto* session = session_table.find_by_endpoint(pkt.remote);

          if (session != nullptr) {
            // Process data from existing session
            session_table.update_activity(session->session_id);
            session->packets_received++;
            session->bytes_received += pkt.data.size();

            if (session->transport) {
              auto frames = session->transport->decrypt_packet(pkt.data);
              if (frames) {
                for (const auto& frame : *frames) {
                  if (frame.kind == mux::FrameKind::kData) {
                    // Write to TUN device
                    if (!tun_device.write(frame.data.payload, ec)) {
                      log_tun_write_error(ec);
                    }
                  } else if (frame.kind == mux::FrameKind::kAck) {
                    session->transport->process_ack(frame.ack);
                  }
                }
              }
            }
          } else {
            // New connection - handle handshake
            auto hs_result = responder.handle_init(pkt.data);
            if (hs_result) {
              if (!udp_socket.send(hs_result->response, pkt.remote, ec)) {
                log_handshake_send_error(ec);
              } else {
                // Create transport session
                auto transport = std::make_unique<transport::TransportSession>(
                    hs_result->session, config.tunnel.transport);

                // Create client session
                auto session_id = session_table.create_session(pkt.remote, std::move(transport));
                if (session_id) {
                  log_new_client(pkt.remote.host, pkt.remote.port, *session_id);
                }
              }
            }
          }
        },
        10, ec);

    // Read from TUN and route to appropriate client
    auto tun_read = tun_device.read_into(buffer, ec);
    if (tun_read > 0) {
      // Parse IP header to find destination
      if (tun_read >= 20) {
        // Extract destination IP from IPv4 header (bytes 16-19)
        std::uint32_t dst_ip = (static_cast<std::uint32_t>(buffer[16]) << 24) |
                               (static_cast<std::uint32_t>(buffer[17]) << 16) |
                               (static_cast<std::uint32_t>(buffer[18]) << 8) |
                               static_cast<std::uint32_t>(buffer[19]);

        // Convert to string
        char ip_str[INET_ADDRSTRLEN];
        struct in_addr addr {};
        addr.s_addr = htonl(dst_ip);
        inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));

        // Find session by tunnel IP
        auto* session = session_table.find_by_tunnel_ip(ip_str);
        if (session != nullptr && session->transport) {
          // Encrypt and send
          auto packets = session->transport->encrypt_data(
              std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(tun_read)));
          for (const auto& pkt : packets) {
            if (!udp_socket.send(pkt, session->endpoint, ec)) {
              LOG_ERROR("Failed to send to client: {}", ec.message());
            } else {
              session->packets_sent++;
              session->bytes_sent += pkt.size();
              g_stats.total_packets_sent++;
              g_stats.total_bytes_sent += pkt.size();
            }
          }
        }
      }
    }

    // Periodic session cleanup
    auto now = std::chrono::steady_clock::now();
    if (now - last_cleanup >= config.cleanup_interval) {
      auto expired = session_table.cleanup_expired();
      if (expired > 0) {
        if (g_stats.connections_active >= expired) {
          g_stats.connections_active -= expired;
        } else {
          g_stats.connections_active = 0;
        }
        cli::print_info("Cleaned up " + std::to_string(expired) + " expired session(s)");
        LOG_INFO("Cleaned up {} expired sessions", expired);
      }
      last_cleanup = now;
    }

    // Periodic stats display (every 60 seconds in verbose mode)
    if (config.verbose && (now - last_stats >= std::chrono::seconds(60))) {
      print_server_status(config.max_clients);
      last_stats = now;
    }

    // Process retransmits for all sessions (use for_each_session to avoid use-after-free)
    session_table.for_each_session([&](server::ClientSession* session) {
      if (session->transport) {
        auto retransmits = session->transport->get_retransmit_packets();
        for (const auto& pkt : retransmits) {
          if (!udp_socket.send(pkt, session->endpoint, ec)) {
            log_retransmit_error(ec);
          }
        }
      }
    });
  }

  // Cleanup
  std::cout << '\n';
  cli::print_section("Shutdown");
  cli::print_info("Cleaning up routes and NAT...");
  LOG_INFO("Shutting down...");
  route_manager.cleanup();

  // Print final stats
  if (!config.daemon_mode) {
    print_server_status(config.max_clients);
  }

  cli::print_success("VEIL Server stopped gracefully");
  LOG_INFO("VEIL Server stopped");
  return EXIT_SUCCESS;
}
