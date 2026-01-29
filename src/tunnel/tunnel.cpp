#include "tunnel/tunnel.h"

#include <array>
#include <fstream>

#include "common/handshake/handshake_processor.h"
#include "transport/mux/mux_codec.h"
#include "common/logging/logger.h"
#ifndef _WIN32
#include "common/signal/signal_handler.h"
#else
#include "windows/console_handler.h"
#endif
#include "common/utils/rate_limiter.h"

namespace veil::tunnel {

namespace {
constexpr std::size_t kMaxPacketSize = 65535;

// Helper functions for ACK sending logging (Issue #72 fix)
// These avoid the bugprone-lambda-function-name clang-tidy warning when LOG_* is used in lambdas
void log_ack_send_error(const std::error_code& ec) {
  LOG_WARN("Failed to send ACK to server: {}", ec.message());
}

void log_ack_sent([[maybe_unused]] std::uint64_t ack, [[maybe_unused]] std::uint32_t bitmap) {
  LOG_DEBUG("Sent ACK to server: ack={}, bitmap={:#010x}", ack, bitmap);
}

// Helper to provide actionable error message for key file issues.
std::string format_key_error(const std::string& key_type, const std::string& path,
                             const std::error_code& ec) {
  std::string msg = key_type + " file '" + path + "' error: " + ec.message() + "\n";
  if (ec == std::errc::no_such_file_or_directory) {
    msg += "  To generate a new key, run:\n";
    msg += "    head -c 32 /dev/urandom > " + path + "\n";
    msg += "  Then copy this file securely to both server and client.";
  } else if (ec == std::errc::permission_denied) {
    msg += "  Check file permissions with: ls -la " + path + "\n";
    msg += "  Ensure the file is readable by the current user.";
  } else if (ec == std::errc::io_error) {
    msg += "  The key file must be exactly 32 bytes.\n";
    msg += "  Regenerate with: head -c 32 /dev/urandom > " + path;
  }
  return msg;
}

bool load_key_from_file(const std::string& path, std::vector<std::uint8_t>& key,
                        std::error_code& ec) {
  LOG_DEBUG("Loading key from file: {}", path);
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    ec = std::error_code(errno, std::generic_category());
    LOG_ERROR("Failed to open key file '{}': {}", path, ec.message());
    return false;
  }

  // First, determine the file size
  file.seekg(0, std::ios::end);
  auto file_size = file.tellg();
  file.seekg(0, std::ios::beg);
  LOG_DEBUG("Key file size: {} bytes", static_cast<long long>(file_size));

  if (file_size < 32) {
    LOG_ERROR("Key file '{}' is too small: {} bytes (expected at least 32)", path,
              static_cast<long long>(file_size));
    ec = std::make_error_code(std::errc::io_error);
    return false;
  }

  if (file_size > 32) {
    LOG_WARN("Key file '{}' is {} bytes (expected 32). Only first 32 bytes will be used.",
             path, static_cast<long long>(file_size));

    // Read all bytes to show what extra data is present
    std::vector<std::uint8_t> all_bytes(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(all_bytes.data()), file_size);

    // Log hex dump of extra bytes to diagnose the issue
    if (file.gcount() == file_size && file_size > 32) {
      std::string extra_hex;
      for (size_t i = 32; i < static_cast<size_t>(file_size) && i < 44; ++i) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02x ", all_bytes[i]);
        extra_hex += buf;
      }
      LOG_WARN("  Extra bytes after position 32: {}", extra_hex);
      LOG_WARN("  This likely means the key file has Windows line endings (CRLF) or was edited in Notepad.");
      LOG_WARN("  FIX: The key should be exactly 32 bytes with no newlines or extra characters.");
      LOG_WARN("  To fix: Copy the raw hex/base64 value WITHOUT any newlines or spaces.");
    }

    // Copy first 32 bytes to key
    key.assign(all_bytes.begin(), all_bytes.begin() + 32);
  } else {
    key.resize(32);
    file.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
    if (file.gcount() != static_cast<std::streamsize>(key.size())) {
      LOG_ERROR("Failed to read 32 bytes from key file '{}': only {} bytes read",
                path, file.gcount());
      ec = std::make_error_code(std::errc::io_error);
      return false;
    }
  }

  LOG_DEBUG("Successfully read 32 bytes from key file");

  // Log hex dump of key for debugging (first 8 bytes only for security)
  if (key.size() >= 8) {
    LOG_DEBUG("Key file content (first 8 bytes): {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
              key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7]);
  }

  return true;
}
}  // namespace

Tunnel::Tunnel(TunnelConfig config, std::function<TimePoint()> now_fn)
    : config_(std::move(config)), now_fn_(std::move(now_fn)), pmtu_discovery_(config_.pmtu, now_fn_) {}

Tunnel::~Tunnel() { stop(); }

bool Tunnel::initialize(std::error_code& ec) {
  LOG_INFO("Initializing tunnel...");

  // Load pre-shared key if specified.
  if (!config_.key_file.empty()) {
    if (!load_key_from_file(config_.key_file, config_.psk, ec)) {
      std::string error_msg = format_key_error("Pre-shared key", config_.key_file, ec);
      LOG_ERROR("{}", error_msg);
      return false;
    }
    LOG_DEBUG("Loaded pre-shared key from {}", config_.key_file);
    // Log key fingerprint (first 4 bytes as hex) for debugging key mismatch issues
    // This helps verify the same key is being used on client and server
    // without revealing the full key
    if (config_.psk.size() >= 4) {
      LOG_DEBUG("PSK fingerprint (first 4 bytes): {:02x}{:02x}{:02x}{:02x}",
                config_.psk[0], config_.psk[1], config_.psk[2], config_.psk[3]);
    }
  } else {
    LOG_WARN("No pre-shared key file specified - connection will fail!");
  }

  // Generate ephemeral key pair for this session.
  key_pair_ = crypto::generate_x25519_keypair();
  LOG_DEBUG("Generated ephemeral key pair");

  // Load obfuscation seed if specified.
  if (!config_.obfuscation_seed_file.empty()) {
    std::vector<std::uint8_t> seed;
    if (!load_key_from_file(config_.obfuscation_seed_file, seed, ec)) {
      LOG_WARN("Failed to load obfuscation seed: {}", ec.message());
      // Continue without obfuscation seed.
    } else if (seed.size() >= obfuscation::kProfileSeedSize) {
      std::copy_n(seed.begin(), obfuscation::kProfileSeedSize,
                  obfuscation_profile_.profile_seed.begin());
      obfuscation_profile_.enabled = true;
      LOG_DEBUG("Loaded obfuscation seed");
    }
  }

  // NOTE: TUN device creation is deferred until after successful handshake
  // to avoid routing conflicts where handshake packets would be sent through
  // the VPN tunnel interface instead of the physical network interface.

  // Open UDP socket.
  if (!udp_socket_.open(config_.local_port, true, ec)) {
    LOG_ERROR("Failed to open UDP socket: {}", ec.message());
    return false;
  }
  // Log both requested and actual bound port (they differ if requested port was 0)
  std::uint16_t actual_port = udp_socket_.local_port();
  if (config_.local_port == 0) {
    LOG_INFO("UDP socket opened on random port {} (requested port 0)", actual_port);
  } else {
    LOG_INFO("UDP socket opened on port {}", actual_port);
  }

  // Create event loop.
  event_loop_ = std::make_unique<transport::EventLoop>(config_.event_loop, now_fn_);

  // Setup PMTU change callback.
  pmtu_discovery_.set_mtu_change_callback(
      [this](const std::string& peer, int old_mtu, int new_mtu) {
        handle_mtu_change(peer, old_mtu, new_mtu);
      });

  set_state(ConnectionState::kDisconnected);
  LOG_INFO("Tunnel initialized successfully");
  return true;
}

void Tunnel::run() {
  if (running_.load()) {
    return;
  }

  running_.store(true);
  LOG_INFO("Tunnel starting...");

#ifdef _WIN32
  // Setup console control handler (Windows).
  auto& console_handler = windows::ConsoleHandler::instance();
  if (!console_handler.setup()) {
    LOG_ERROR("Failed to setup console control handler");
    running_.store(false);
    return;
  }
#else
  // Setup signal handlers (POSIX only).
  auto& sig_handler = signal::SignalHandler::instance();
  sig_handler.setup_defaults();
#endif

  // Connect to server (for client mode).
  if (!config_.server_address.empty()) {
    set_state(ConnectionState::kConnecting);

    std::error_code ec;
    transport::UdpEndpoint remote{config_.server_address, config_.server_port};
    if (!udp_socket_.connect(remote, ec)) {
      LOG_ERROR("Failed to connect to server: {}", ec.message());
      if (error_callback_) {
        error_callback_("Failed to connect: " + ec.message());
      }
      set_state(ConnectionState::kDisconnected);
      running_.store(false);
      return;
    }

    // Perform handshake.
    set_state(ConnectionState::kHandshaking);
    if (!perform_handshake(ec)) {
      LOG_ERROR("Handshake failed: {}", ec.message());
      if (error_callback_) {
        error_callback_("Handshake failed: " + ec.message());
      }
      handle_reconnect();
    } else {
      // Handshake succeeded - now create TUN device.
      LOG_INFO("Creating TUN device after successful handshake...");
      if (!tun_device_.open(config_.tun, ec)) {
        LOG_ERROR("Failed to open TUN device: {}", ec.message());
        if (error_callback_) {
          error_callback_("Failed to open TUN device: " + ec.message());
        }
        set_state(ConnectionState::kDisconnected);
        running_.store(false);
        return;
      }
      LOG_INFO("TUN device {} opened with IP {}", tun_device_.device_name(), config_.tun.ip_address);

      set_state(ConnectionState::kConnected);
      stats_.connected_since = now_fn_();
    }
  }

  // Main event loop.
  std::array<std::uint8_t, kMaxPacketSize> tun_buffer{};

  // Diagnostic counters for periodic logging
  std::uint64_t loop_iterations = 0;
  std::uint64_t last_logged_tx = 0;
  std::uint64_t last_logged_rx = 0;
  auto last_diagnostic_log = now_fn_();

  while (running_.load()
#ifdef _WIN32
         && !console_handler.should_terminate()
#else
         && !sig_handler.should_terminate()
#endif
  ) {
    std::error_code ec;
    loop_iterations++;

    // Check TUN device for incoming packets (only if device is open).
    if (tun_device_.is_open()) {
      auto tun_read = tun_device_.read_into(tun_buffer, ec);
      if (tun_read > 0) {
        on_tun_packet(std::span<const std::uint8_t>(tun_buffer.data(), static_cast<std::size_t>(tun_read)));
      } else if (tun_read < 0) {
        LOG_ERROR("TUN read error: {}", ec.message());
        stats_.tun_read_errors++;
      }
    }

    // Poll UDP socket for incoming packets.
    if (!udp_socket_.poll(
        [this](const transport::UdpPacket& pkt) {
          on_udp_packet(pkt.data, pkt.remote);
        },
        10, ec)) {
      LOG_ERROR("UDP poll failed: {}", ec.message());
    }

    // Periodic diagnostic logging (every 5 seconds when connected)
    auto now = now_fn_();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_diagnostic_log);
    if (elapsed.count() >= 5 && state_.load() == ConnectionState::kConnected) {
      last_diagnostic_log = now;

      // Log traffic stats if they changed
      if (stats_.udp_packets_sent != last_logged_tx || stats_.udp_packets_received != last_logged_rx) {
        LOG_INFO("Tunnel stats: packets_sent={}, packets_received={}, loops={}, decrypt_errors={}, encrypt_errors={}",
                 stats_.udp_packets_sent, stats_.udp_packets_received, loop_iterations,
                 stats_.decrypt_errors, stats_.encrypt_errors);
        last_logged_tx = stats_.udp_packets_sent;
        last_logged_rx = stats_.udp_packets_received;
      }

      // Warn if we're sending but not receiving
      if (stats_.udp_packets_sent > 10 && stats_.udp_packets_received == 0) {
        LOG_WARN("WARNING: Sending packets but receiving none! Check firewall and server connectivity.");
        LOG_WARN("  - Packets sent: {}, Packets received: {}", stats_.udp_packets_sent, stats_.udp_packets_received);
        LOG_WARN("  - Server: {}:{}", config_.server_address, config_.server_port);
      }
    }

    // Process session timers if we have an active session.
    if (session_) {
      // Check for retransmits.
      auto retransmits = session_->get_retransmit_packets();
      for (const auto& pkt : retransmits) {
        std::error_code send_ec;
        transport::UdpEndpoint remote{config_.server_address, config_.server_port};
        if (!udp_socket_.send(pkt, remote, send_ec)) {
          LOG_WARN("Failed to send retransmit: {}", send_ec.message());
        }
      }

      // Check for session rotation.
      if (session_->should_rotate_session()) {
        session_->rotate_session();
        LOG_DEBUG("Session rotated");
      }
    }

    // Handle reconnection if needed.
    if (state_.load() == ConnectionState::kReconnecting) {
      handle_reconnect();
    }

    stats_.last_activity = now_fn_();
  }

  LOG_INFO("Tunnel stopping...");
  set_state(ConnectionState::kDisconnected);
  running_.store(false);
}

void Tunnel::stop() {
  running_.store(false);
  if (event_loop_) {
    event_loop_->stop();
  }
  LOG_INFO("Tunnel stopped");
}

void Tunnel::on_tun_packet(std::span<const std::uint8_t> packet) {
  stats_.tun_packets_received++;
  stats_.tun_bytes_received += packet.size();

  if (!session_ || state_.load() != ConnectionState::kConnected) {
    return;
  }

  // Encrypt and send through UDP.
  auto encrypted_packets = session_->encrypt_data(packet);
  for (const auto& enc_pkt : encrypted_packets) {
    std::error_code ec;
    transport::UdpEndpoint remote{config_.server_address, config_.server_port};
    if (!udp_socket_.send(enc_pkt, remote, ec)) {
      LOG_WARN("Failed to send encrypted packet: {}", ec.message());
      stats_.encrypt_errors++;
      continue;
    }
    stats_.udp_packets_sent++;
    stats_.udp_bytes_sent += enc_pkt.size();
  }
}

void Tunnel::on_udp_packet(std::span<const std::uint8_t> packet,
                            const transport::UdpEndpoint& remote) {
  stats_.udp_packets_received++;
  stats_.udp_bytes_received += packet.size();

  if (!session_) {
    return;
  }

  // Decrypt the packet.
  auto frames = session_->decrypt_packet(packet);
  if (!frames) {
    LOG_DEBUG("Failed to decrypt packet from {}:{}", remote.host, remote.port);
    stats_.decrypt_errors++;
    return;
  }

  // Process each frame.
  for (const auto& frame : *frames) {
    if (frame.kind == mux::FrameKind::kData) {
      // Write decrypted data to TUN device.
      std::error_code ec;
      if (!tun_device_.write(frame.data.payload, ec)) {
        LOG_ERROR("Failed to write to TUN: {}", ec.message());
        stats_.tun_write_errors++;
        continue;
      }
      stats_.tun_packets_sent++;
      stats_.tun_bytes_sent += frame.data.payload.size();

      // Log successful TUN write for diagnostics (helps debug Issue #74)
      // Changed to DEBUG level to avoid performance impact in hot path (Issue #92)
      if (frame.data.payload.size() >= 20) {
        // Extract destination IP from IPv4 header for logging
        [[maybe_unused]] std::uint32_t dst_ip = 0;
        dst_ip |= static_cast<std::uint32_t>(frame.data.payload[16]) << 24;
        dst_ip |= static_cast<std::uint32_t>(frame.data.payload[17]) << 16;
        dst_ip |= static_cast<std::uint32_t>(frame.data.payload[18]) << 8;
        dst_ip |= static_cast<std::uint32_t>(frame.data.payload[19]);
        LOG_DEBUG("TUN write: {} bytes -> {}.{}.{}.{}",
                  frame.data.payload.size(),
                  (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
                  (dst_ip >> 8) & 0xFF, dst_ip & 0xFF);
      } else {
        LOG_DEBUG("TUN write: {} bytes (packet too small for IPv4)", frame.data.payload.size());
      }

      // Send ACK back to server (Issue #72 fix)
      // Without ACKs, the server would keep retransmitting packets
      // IMPORTANT: Use encrypt_frame() instead of encrypt_data() to preserve the ACK frame kind.
      // encrypt_data() wraps data in a DATA frame, which would cause the receiver to
      // incorrectly interpret the ACK as data and try to write it to TUN.
      auto ack_info = session_->generate_ack(frame.data.stream_id);
      auto ack_frame = mux::make_ack_frame(ack_info.stream_id, ack_info.ack, ack_info.bitmap);
      auto ack_packet = session_->encrypt_frame(ack_frame);
      transport::UdpEndpoint server_endpoint{config_.server_address, config_.server_port};
      std::error_code send_ec;
      if (!udp_socket_.send(ack_packet, server_endpoint, send_ec)) {
        log_ack_send_error(send_ec);
      } else {
        log_ack_sent(ack_info.ack, ack_info.bitmap);
      }
    } else if (frame.kind == mux::FrameKind::kAck) {
      session_->process_ack(frame.ack);
    }
  }

  // Update PMTU discovery.
  pmtu_discovery_.handle_probe_success(remote.host, static_cast<int>(packet.size()));
}

bool Tunnel::perform_handshake(std::error_code& ec) {
  LOG_INFO("Performing handshake with {}:{}", config_.server_address, config_.server_port);

  // Validate PSK before attempting handshake
  if (config_.psk.empty()) {
    ec = std::make_error_code(std::errc::invalid_argument);
    LOG_ERROR("HANDSHAKE: PSK is empty - cannot perform handshake");
    return false;
  }
  if (config_.psk.size() != 32) {
    ec = std::make_error_code(std::errc::invalid_argument);
    LOG_ERROR("HANDSHAKE: PSK size is {} bytes (expected 32)", config_.psk.size());
    return false;
  }
  LOG_DEBUG("HANDSHAKE: PSK validated (32 bytes)");

  // Create handshake initiator.
  handshake::HandshakeInitiator initiator(config_.psk, config_.handshake_skew_tolerance);

  // Generate INIT message.
  auto init_msg = initiator.create_init();
  if (init_msg.empty()) {
    ec = std::make_error_code(std::errc::protocol_error);
    LOG_ERROR("Failed to create handshake INIT message");
    return false;
  }

  LOG_INFO("========================================");
  LOG_INFO("HANDSHAKE: Generated INIT message");
  LOG_INFO("  Size: {} bytes", init_msg.size());
  LOG_INFO("  Target: {}:{}", config_.server_address, config_.server_port);
  // Log first few bytes of encrypted INIT for debugging (nonce is public, safe to log)
  if (init_msg.size() >= 12) {
    LOG_DEBUG("  Nonce (first 12 bytes): {:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
              init_msg[0], init_msg[1], init_msg[2], init_msg[3],
              init_msg[4], init_msg[5], init_msg[6], init_msg[7],
              init_msg[8], init_msg[9], init_msg[10], init_msg[11]);
  }
  LOG_INFO("========================================");

  // Send INIT message.
  transport::UdpEndpoint remote{config_.server_address, config_.server_port};
  if (!udp_socket_.send(init_msg, remote, ec)) {
    LOG_ERROR("HANDSHAKE: Failed to send INIT: {}", ec.message());
    return false;
  }
  LOG_INFO("HANDSHAKE: INIT sent successfully, waiting for RESPONSE...");

  // Wait for RESPONSE.
  // Use short polling intervals to allow checking running_ flag and respond quickly to stop().
  std::vector<std::uint8_t> response;
  bool received = false;
  transport::UdpEndpoint response_endpoint;
  int timeout_ms = static_cast<int>(config_.handshake_skew_tolerance.count());
  LOG_DEBUG("HANDSHAKE: Polling for response (timeout: {}ms)", timeout_ms);

  auto start_time = now_fn_();
  const auto timeout_duration = std::chrono::milliseconds(timeout_ms);
  constexpr int poll_interval_ms = 100;  // Poll in 100ms chunks to check running_ flag.

  while (!received && running_.load()) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now_fn_() - start_time);
    if (elapsed >= timeout_duration) {
      break;  // Timeout reached.
    }

    // Calculate remaining time, but don't exceed poll_interval_ms.
    auto remaining = timeout_duration - elapsed;
    int current_poll_ms = std::min(poll_interval_ms, static_cast<int>(remaining.count()));

    udp_socket_.poll(
        [&response, &received, &response_endpoint](const transport::UdpPacket& pkt) {
          response = pkt.data;
          response_endpoint = pkt.remote;
          received = true;
        },
        current_poll_ms, ec);

    if (received) {
      break;  // Got response!
    }

    // Check if we should stop (user pressed disconnect).
    if (!running_.load()) {
      LOG_INFO("HANDSHAKE: Aborted by user disconnect");
      ec = std::make_error_code(std::errc::operation_canceled);
      return false;
    }
  }

  if (!received || response.empty()) {
    ec = std::make_error_code(std::errc::timed_out);
    LOG_ERROR("HANDSHAKE: Timeout waiting for RESPONSE after {}ms", timeout_ms);
    LOG_ERROR("HANDSHAKE: No packets received from server");
    return false;
  }

  LOG_INFO("HANDSHAKE: Received packet from {}:{}, size: {} bytes",
           response_endpoint.host, response_endpoint.port, response.size());

  // Process RESPONSE.
  auto hs_session = initiator.consume_response(response);
  if (!hs_session) {
    ec = std::make_error_code(std::errc::protocol_error);
    LOG_ERROR("Failed to process handshake RESPONSE");
    return false;
  }

  // Create transport session from handshake result.
  session_ = std::make_unique<transport::TransportSession>(*hs_session, config_.transport, now_fn_);

  LOG_INFO("Handshake completed successfully, session ID: {}", session_->session_id());
  return true;
}

void Tunnel::set_state(ConnectionState new_state) {
  ConnectionState old_state = state_.exchange(new_state);
  if (old_state != new_state) {
    LOG_INFO("Connection state: {} -> {}",
             static_cast<int>(old_state), static_cast<int>(new_state));
    if (state_change_callback_) {
      state_change_callback_(old_state, new_state);
    }
  }
}

bool Tunnel::send_packet(std::span<const std::uint8_t> data) {
  if (!session_ || state_.load() != ConnectionState::kConnected) {
    return false;
  }

  auto encrypted_packets = session_->encrypt_data(data);
  for (const auto& pkt : encrypted_packets) {
    std::error_code ec;
    transport::UdpEndpoint remote{config_.server_address, config_.server_port};
    if (!udp_socket_.send(pkt, remote, ec)) {
      return false;
    }
  }
  return true;
}

void Tunnel::handle_reconnect() {
  // Check if tunnel has been stopped - don't reconnect if user disconnected.
  if (!running_.load()) {
    LOG_DEBUG("Reconnect aborted: tunnel stopped");
    set_state(ConnectionState::kDisconnected);
    return;
  }

  if (!config_.auto_reconnect) {
    set_state(ConnectionState::kDisconnected);
    return;
  }

  // Check if we've exceeded max attempts.
  if (config_.max_reconnect_attempts > 0 && reconnect_attempts_ >= config_.max_reconnect_attempts) {
    LOG_ERROR("Max reconnection attempts ({}) exceeded", config_.max_reconnect_attempts);
    set_state(ConnectionState::kDisconnected);
    if (error_callback_) {
      error_callback_("Max reconnection attempts exceeded");
    }
    return;
  }

  // Check if enough time has passed since last attempt.
  auto now = now_fn_();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_reconnect_attempt_);
  if (elapsed < config_.reconnect_delay) {
    return;
  }

  reconnect_attempts_++;
  last_reconnect_attempt_ = now;
  LOG_INFO("Reconnection attempt {} (delay: {}ms)", reconnect_attempts_,
           config_.reconnect_delay.count());

  set_state(ConnectionState::kConnecting);

  // Re-initialize socket.
  udp_socket_.close();
  std::error_code ec;
  if (!udp_socket_.open(config_.local_port, true, ec)) {
    LOG_ERROR("Failed to reopen UDP socket: {}", ec.message());
    set_state(ConnectionState::kReconnecting);
    return;
  }

  // Reconnect.
  transport::UdpEndpoint remote{config_.server_address, config_.server_port};
  if (!udp_socket_.connect(remote, ec)) {
    LOG_ERROR("Failed to reconnect: {}", ec.message());
    set_state(ConnectionState::kReconnecting);
    return;
  }

  // Perform handshake.
  set_state(ConnectionState::kHandshaking);
  if (!perform_handshake(ec)) {
    LOG_ERROR("Reconnection handshake failed: {}", ec.message());
    set_state(ConnectionState::kReconnecting);
    return;
  }

  // Handshake succeeded - create TUN device if not already created.
  if (!tun_device_.is_open()) {
    LOG_INFO("Creating TUN device after successful reconnection...");
    if (!tun_device_.open(config_.tun, ec)) {
      LOG_ERROR("Failed to open TUN device: {}", ec.message());
      set_state(ConnectionState::kReconnecting);
      return;
    }
    LOG_INFO("TUN device {} opened with IP {}", tun_device_.device_name(), config_.tun.ip_address);
  }

  // Success!
  reconnect_attempts_ = 0;
  stats_.reconnect_count++;
  stats_.connected_since = now_fn_();
  set_state(ConnectionState::kConnected);
  LOG_INFO("Reconnected successfully");
}

void Tunnel::on_state_change(StateChangeCallback callback) {
  state_change_callback_ = std::move(callback);
}

void Tunnel::on_error(ErrorCallback callback) { error_callback_ = std::move(callback); }

void Tunnel::handle_mtu_change(const std::string& peer, int old_mtu, int new_mtu) {
  LOG_INFO("MTU changed for {}: {} -> {}", peer, old_mtu, new_mtu);
  // Update TUN device MTU.
  std::error_code mtu_ec;
  if (!tun_device_.set_mtu(new_mtu, mtu_ec)) {
    LOG_WARN("Failed to update TUN MTU: {}", mtu_ec.message());
  }
}

}  // namespace veil::tunnel
