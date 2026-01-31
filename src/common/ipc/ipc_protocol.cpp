#include "ipc_protocol.h"

#include <iostream>
#include <type_traits>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace veil::ipc {

// ============================================================================
// Helper Functions
// ============================================================================

const char* connection_state_to_string(ConnectionState state) {
  switch (state) {
    case ConnectionState::kDisconnected: return "disconnected";
    case ConnectionState::kConnecting: return "connecting";
    case ConnectionState::kConnected: return "connected";
    case ConnectionState::kReconnecting: return "reconnecting";
    case ConnectionState::kError: return "error";
  }
  return "unknown";
}

std::optional<ConnectionState> connection_state_from_string(const std::string& str) {
  if (str == "disconnected") return ConnectionState::kDisconnected;
  if (str == "connecting") return ConnectionState::kConnecting;
  if (str == "connected") return ConnectionState::kConnected;
  if (str == "reconnecting") return ConnectionState::kReconnecting;
  if (str == "error") return ConnectionState::kError;
  return std::nullopt;
}

// ============================================================================
// JSON Conversion Functions
// ============================================================================

// ConnectionConfig
void to_json(json& j, const ConnectionConfig& cfg) {
  j = json{
    {"server_address", cfg.server_address},
    {"server_port", cfg.server_port},
    {"enable_obfuscation", cfg.enable_obfuscation},
    {"auto_reconnect", cfg.auto_reconnect},
    {"reconnect_interval_sec", cfg.reconnect_interval_sec},
    {"max_reconnect_attempts", cfg.max_reconnect_attempts},
    {"route_all_traffic", cfg.route_all_traffic},
    {"custom_routes", cfg.custom_routes},
    {"key_file", cfg.key_file},
    {"obfuscation_seed_file", cfg.obfuscation_seed_file},
    {"tun_device_name", cfg.tun_device_name},
    {"tun_ip_address", cfg.tun_ip_address},
    {"tun_netmask", cfg.tun_netmask},
    {"tun_mtu", cfg.tun_mtu}
  };
}

void from_json(const json& j, ConnectionConfig& cfg) {
  j.at("server_address").get_to(cfg.server_address);
  j.at("server_port").get_to(cfg.server_port);
  j.at("enable_obfuscation").get_to(cfg.enable_obfuscation);
  j.at("auto_reconnect").get_to(cfg.auto_reconnect);
  j.at("reconnect_interval_sec").get_to(cfg.reconnect_interval_sec);
  j.at("max_reconnect_attempts").get_to(cfg.max_reconnect_attempts);
  j.at("route_all_traffic").get_to(cfg.route_all_traffic);
  j.at("custom_routes").get_to(cfg.custom_routes);
  // Optional fields with defaults
  if (j.contains("key_file")) {
    j.at("key_file").get_to(cfg.key_file);
  }
  if (j.contains("obfuscation_seed_file")) {
    j.at("obfuscation_seed_file").get_to(cfg.obfuscation_seed_file);
  }
  if (j.contains("tun_device_name")) {
    j.at("tun_device_name").get_to(cfg.tun_device_name);
  }
  if (j.contains("tun_ip_address")) {
    j.at("tun_ip_address").get_to(cfg.tun_ip_address);
  }
  if (j.contains("tun_netmask")) {
    j.at("tun_netmask").get_to(cfg.tun_netmask);
  }
  if (j.contains("tun_mtu")) {
    j.at("tun_mtu").get_to(cfg.tun_mtu);
  }
}

// ConnectionStatus
void to_json(json& j, const ConnectionStatus& status) {
  j = json{
    {"state", connection_state_to_string(status.state)},
    {"session_id", status.session_id},
    {"server_address", status.server_address},
    {"server_port", status.server_port},
    {"uptime_sec", status.uptime_sec},
    {"error_message", status.error_message},
    {"reconnect_attempt", status.reconnect_attempt}
  };
}

void from_json(const json& j, ConnectionStatus& status) {
  auto state_str = j.at("state").get<std::string>();
  auto state_opt = connection_state_from_string(state_str);
  status.state = state_opt.value_or(ConnectionState::kDisconnected);
  j.at("session_id").get_to(status.session_id);
  j.at("server_address").get_to(status.server_address);
  j.at("server_port").get_to(status.server_port);
  j.at("uptime_sec").get_to(status.uptime_sec);
  j.at("error_message").get_to(status.error_message);
  j.at("reconnect_attempt").get_to(status.reconnect_attempt);
}

// ConnectionMetrics
void to_json(json& j, const ConnectionMetrics& metrics) {
  j = json{
    {"latency_ms", metrics.latency_ms},
    {"tx_bytes_per_sec", metrics.tx_bytes_per_sec},
    {"rx_bytes_per_sec", metrics.rx_bytes_per_sec},
    {"total_tx_bytes", metrics.total_tx_bytes},
    {"total_rx_bytes", metrics.total_rx_bytes}
  };
}

void from_json(const json& j, ConnectionMetrics& metrics) {
  j.at("latency_ms").get_to(metrics.latency_ms);
  j.at("tx_bytes_per_sec").get_to(metrics.tx_bytes_per_sec);
  j.at("rx_bytes_per_sec").get_to(metrics.rx_bytes_per_sec);
  j.at("total_tx_bytes").get_to(metrics.total_tx_bytes);
  j.at("total_rx_bytes").get_to(metrics.total_rx_bytes);
}

// ProtocolMetrics
void to_json(json& j, const ProtocolMetrics& metrics) {
  j = json{
    {"send_sequence", metrics.send_sequence},
    {"recv_sequence", metrics.recv_sequence},
    {"packets_sent", metrics.packets_sent},
    {"packets_received", metrics.packets_received},
    {"packets_lost", metrics.packets_lost},
    {"packets_retransmitted", metrics.packets_retransmitted},
    {"loss_percentage", metrics.loss_percentage}
  };
}

void from_json(const json& j, ProtocolMetrics& metrics) {
  j.at("send_sequence").get_to(metrics.send_sequence);
  j.at("recv_sequence").get_to(metrics.recv_sequence);
  j.at("packets_sent").get_to(metrics.packets_sent);
  j.at("packets_received").get_to(metrics.packets_received);
  j.at("packets_lost").get_to(metrics.packets_lost);
  j.at("packets_retransmitted").get_to(metrics.packets_retransmitted);
  j.at("loss_percentage").get_to(metrics.loss_percentage);
}

// ReassemblyStats
void to_json(json& j, const ReassemblyStats& stats) {
  j = json{
    {"fragments_received", stats.fragments_received},
    {"messages_reassembled", stats.messages_reassembled},
    {"fragments_pending", stats.fragments_pending},
    {"reassembly_timeouts", stats.reassembly_timeouts}
  };
}

void from_json(const json& j, ReassemblyStats& stats) {
  j.at("fragments_received").get_to(stats.fragments_received);
  j.at("messages_reassembled").get_to(stats.messages_reassembled);
  j.at("fragments_pending").get_to(stats.fragments_pending);
  j.at("reassembly_timeouts").get_to(stats.reassembly_timeouts);
}

// ObfuscationProfile
void to_json(json& j, const ObfuscationProfile& profile) {
  j = json{
    {"padding_enabled", profile.padding_enabled},
    {"current_padding_size", profile.current_padding_size},
    {"timing_jitter_model", profile.timing_jitter_model},
    {"timing_jitter_param", profile.timing_jitter_param},
    {"heartbeat_mode", profile.heartbeat_mode},
    {"last_heartbeat_sec", profile.last_heartbeat_sec}
  };
}

void from_json(const json& j, const ObfuscationProfile& profile) {
  // Note: This is intentionally empty as ObfuscationProfile is read-only from GUI
  (void)j;
  (void)profile;
}

// ClientSession
void to_json(json& j, const ClientSession& session) {
  j = json{
    {"session_id", session.session_id},
    {"tunnel_ip", session.tunnel_ip},
    {"endpoint_host", session.endpoint_host},
    {"endpoint_port", session.endpoint_port},
    {"uptime_sec", session.uptime_sec},
    {"packets_sent", session.packets_sent},
    {"packets_received", session.packets_received},
    {"bytes_sent", session.bytes_sent},
    {"bytes_received", session.bytes_received},
    {"last_activity_sec", session.last_activity_sec}
  };
}

// ServerStatus
void to_json(json& j, const ServerStatus& status) {
  j = json{
    {"running", status.running},
    {"listen_port", status.listen_port},
    {"listen_address", status.listen_address},
    {"active_clients", status.active_clients},
    {"max_clients", status.max_clients},
    {"uptime_sec", status.uptime_sec},
    {"total_packets_sent", status.total_packets_sent},
    {"total_packets_received", status.total_packets_received},
    {"total_bytes_sent", status.total_bytes_sent},
    {"total_bytes_received", status.total_bytes_received}
  };
}

// ============================================================================
// JSON Conversion - LogEvent
// ============================================================================

void to_json(json& j, const LogEvent& event) {
  j = json{
    {"timestamp_ms", event.timestamp_ms},
    {"level", event.level},
    {"message", event.message}
  };
}

void from_json(const json& j, LogEvent& event) {
  if (j.contains("timestamp_ms")) j.at("timestamp_ms").get_to(event.timestamp_ms);
  if (j.contains("level")) j.at("level").get_to(event.level);
  if (j.contains("message")) j.at("message").get_to(event.message);
}

// ============================================================================
// JSON Conversion - HeartbeatEvent
// ============================================================================

void to_json(json& j, const HeartbeatEvent& event) {
  j = json{
    {"timestamp_ms", event.timestamp_ms}
  };
}

void from_json(const json& j, HeartbeatEvent& event) {
  if (j.contains("timestamp_ms")) j.at("timestamp_ms").get_to(event.timestamp_ms);
}

// ============================================================================
// JSON Conversion - DiagnosticsData
// ============================================================================

void to_json(json& j, const DiagnosticsData& diag) {
  j = json{
    {"protocol", json{
      {"send_sequence", diag.protocol.send_sequence},
      {"recv_sequence", diag.protocol.recv_sequence},
      {"packets_sent", diag.protocol.packets_sent},
      {"packets_received", diag.protocol.packets_received},
      {"packets_lost", diag.protocol.packets_lost},
      {"packets_retransmitted", diag.protocol.packets_retransmitted},
      {"loss_percentage", diag.protocol.loss_percentage}
    }},
    {"reassembly", json{
      {"fragments_received", diag.reassembly.fragments_received},
      {"messages_reassembled", diag.reassembly.messages_reassembled},
      {"fragments_pending", diag.reassembly.fragments_pending},
      {"reassembly_timeouts", diag.reassembly.reassembly_timeouts}
    }},
    {"obfuscation", json{
      {"padding_enabled", diag.obfuscation.padding_enabled},
      {"current_padding_size", diag.obfuscation.current_padding_size},
      {"timing_jitter_model", diag.obfuscation.timing_jitter_model},
      {"timing_jitter_param", diag.obfuscation.timing_jitter_param},
      {"heartbeat_mode", diag.obfuscation.heartbeat_mode},
      {"last_heartbeat_sec", diag.obfuscation.last_heartbeat_sec},
      {"active_dpi_mode", diag.obfuscation.active_dpi_mode}
    }},
    {"recent_events", json::array()}
  };
  for (const auto& event : diag.recent_events) {
    json event_json;
    to_json(event_json, event);
    j["recent_events"].push_back(event_json);
  }
}

void from_json(const json& j, DiagnosticsData& diag) {
  if (j.contains("protocol")) {
    const auto& p = j.at("protocol");
    if (p.contains("send_sequence")) p.at("send_sequence").get_to(diag.protocol.send_sequence);
    if (p.contains("recv_sequence")) p.at("recv_sequence").get_to(diag.protocol.recv_sequence);
    if (p.contains("packets_sent")) p.at("packets_sent").get_to(diag.protocol.packets_sent);
    if (p.contains("packets_received")) p.at("packets_received").get_to(diag.protocol.packets_received);
    if (p.contains("packets_lost")) p.at("packets_lost").get_to(diag.protocol.packets_lost);
    if (p.contains("packets_retransmitted")) p.at("packets_retransmitted").get_to(diag.protocol.packets_retransmitted);
    if (p.contains("loss_percentage")) p.at("loss_percentage").get_to(diag.protocol.loss_percentage);
  }
  if (j.contains("reassembly")) {
    const auto& r = j.at("reassembly");
    if (r.contains("fragments_received")) r.at("fragments_received").get_to(diag.reassembly.fragments_received);
    if (r.contains("messages_reassembled")) r.at("messages_reassembled").get_to(diag.reassembly.messages_reassembled);
    if (r.contains("fragments_pending")) r.at("fragments_pending").get_to(diag.reassembly.fragments_pending);
    if (r.contains("reassembly_timeouts")) r.at("reassembly_timeouts").get_to(diag.reassembly.reassembly_timeouts);
  }
  if (j.contains("obfuscation")) {
    const auto& o = j.at("obfuscation");
    if (o.contains("padding_enabled")) o.at("padding_enabled").get_to(diag.obfuscation.padding_enabled);
    if (o.contains("current_padding_size")) o.at("current_padding_size").get_to(diag.obfuscation.current_padding_size);
    if (o.contains("timing_jitter_model")) o.at("timing_jitter_model").get_to(diag.obfuscation.timing_jitter_model);
    if (o.contains("timing_jitter_param")) o.at("timing_jitter_param").get_to(diag.obfuscation.timing_jitter_param);
    if (o.contains("heartbeat_mode")) o.at("heartbeat_mode").get_to(diag.obfuscation.heartbeat_mode);
    if (o.contains("last_heartbeat_sec")) o.at("last_heartbeat_sec").get_to(diag.obfuscation.last_heartbeat_sec);
    if (o.contains("active_dpi_mode")) o.at("active_dpi_mode").get_to(diag.obfuscation.active_dpi_mode);
  }
  if (j.contains("recent_events") && j.at("recent_events").is_array()) {
    for (const auto& event_json : j.at("recent_events")) {
      LogEvent event;
      from_json(event_json, event);
      diag.recent_events.push_back(event);
    }
  }
}

// ============================================================================
// JSON Conversion - ClientSession
// ============================================================================

void from_json(const json& j, ClientSession& session) {
  if (j.contains("session_id")) j.at("session_id").get_to(session.session_id);
  if (j.contains("tunnel_ip")) j.at("tunnel_ip").get_to(session.tunnel_ip);
  if (j.contains("endpoint_host")) j.at("endpoint_host").get_to(session.endpoint_host);
  if (j.contains("endpoint_port")) j.at("endpoint_port").get_to(session.endpoint_port);
  if (j.contains("uptime_sec")) j.at("uptime_sec").get_to(session.uptime_sec);
  if (j.contains("packets_sent")) j.at("packets_sent").get_to(session.packets_sent);
  if (j.contains("packets_received")) j.at("packets_received").get_to(session.packets_received);
  if (j.contains("bytes_sent")) j.at("bytes_sent").get_to(session.bytes_sent);
  if (j.contains("bytes_received")) j.at("bytes_received").get_to(session.bytes_received);
  if (j.contains("last_activity_sec")) j.at("last_activity_sec").get_to(session.last_activity_sec);
}

// ============================================================================
// Command Serialization Helpers
// ============================================================================

namespace {

json serialize_command(const Command& cmd) {
  json payload;

  std::visit([&payload](const auto& c) {
    using T = std::decay_t<decltype(c)>;

    if constexpr (std::is_same_v<T, ConnectCommand>) {
      payload["command_type"] = "connect";
      json config_json;
      to_json(config_json, c.config);
      payload["config"] = config_json;
    }
    else if constexpr (std::is_same_v<T, DisconnectCommand>) {
      payload["command_type"] = "disconnect";
    }
    else if constexpr (std::is_same_v<T, GetStatusCommand>) {
      payload["command_type"] = "get_status";
    }
    else if constexpr (std::is_same_v<T, GetMetricsCommand>) {
      payload["command_type"] = "get_metrics";
    }
    else if constexpr (std::is_same_v<T, GetDiagnosticsCommand>) {
      payload["command_type"] = "get_diagnostics";
    }
    else if constexpr (std::is_same_v<T, UpdateConfigCommand>) {
      payload["command_type"] = "update_config";
      json config_json;
      to_json(config_json, c.config);
      payload["config"] = config_json;
    }
    else if constexpr (std::is_same_v<T, ExportDiagnosticsCommand>) {
      payload["command_type"] = "export_diagnostics";
      payload["export_path"] = c.export_path;
    }
    else if constexpr (std::is_same_v<T, GetClientListCommand>) {
      payload["command_type"] = "get_client_list";
    }
  }, cmd);

  return payload;
}

json serialize_event(const Event& evt) {
  json payload;

  std::visit([&payload](const auto& e) {
    using T = std::decay_t<decltype(e)>;

    if constexpr (std::is_same_v<T, StatusUpdateEvent>) {
      payload["event_type"] = "status_update";
      json status_json;
      to_json(status_json, e.status);
      payload["status"] = status_json;
    }
    else if constexpr (std::is_same_v<T, MetricsUpdateEvent>) {
      payload["event_type"] = "metrics_update";
      json metrics_json;
      to_json(metrics_json, e.metrics);
      payload["metrics"] = metrics_json;
    }
    else if constexpr (std::is_same_v<T, ConnectionStateChangeEvent>) {
      payload["event_type"] = "connection_state_change";
      payload["old_state"] = connection_state_to_string(e.old_state);
      payload["new_state"] = connection_state_to_string(e.new_state);
      payload["message"] = e.message;
    }
    else if constexpr (std::is_same_v<T, ErrorEvent>) {
      payload["event_type"] = "error";
      payload["error_message"] = e.error_message;
      payload["details"] = e.details;
    }
    else if constexpr (std::is_same_v<T, LogEventData>) {
      payload["event_type"] = "log";
      json event_json;
      to_json(event_json, e.event);
      payload["event"] = event_json;
    }
    else if constexpr (std::is_same_v<T, HeartbeatEvent>) {
      payload["event_type"] = "heartbeat";
      payload["timestamp_ms"] = e.timestamp_ms;
    }
    else if constexpr (std::is_same_v<T, ClientListUpdateEvent>) {
      payload["event_type"] = "client_list_update";
      payload["clients"] = json::array();
      for (const auto& client : e.clients) {
        json client_json;
        to_json(client_json, client);
        payload["clients"].push_back(client_json);
      }
    }
    else if constexpr (std::is_same_v<T, ServerStatusUpdateEvent>) {
      payload["event_type"] = "server_status_update";
      json status_json;
      to_json(status_json, e.status);
      payload["status"] = status_json;
    }
  }, evt);

  return payload;
}

json serialize_response(const Response& resp) {
  json payload;

  #ifdef VEIL_IPC_DEBUG
  std::cerr << "[IPC Debug] serialize_response: resp.index=" << resp.index() << std::endl;
  #endif

  std::visit([&payload](const auto& r) {
    using T = std::decay_t<decltype(r)>;

    if constexpr (std::is_same_v<T, StatusResponse>) {
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Debug] Serializing StatusResponse" << std::endl;
      #endif
      payload["response_type"] = "status";
      json status_json;
      to_json(status_json, r.status);
      payload["status"] = status_json;
    }
    else if constexpr (std::is_same_v<T, MetricsResponse>) {
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Debug] Serializing MetricsResponse" << std::endl;
      #endif
      payload["response_type"] = "metrics";
      json metrics_json;
      to_json(metrics_json, r.metrics);
      payload["metrics"] = metrics_json;
    }
    else if constexpr (std::is_same_v<T, DiagnosticsResponse>) {
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Debug] Serializing DiagnosticsResponse" << std::endl;
      #endif
      payload["response_type"] = "diagnostics";
      json diag_json;
      to_json(diag_json, r.diagnostics);
      payload["diagnostics"] = diag_json;
    }
    else if constexpr (std::is_same_v<T, ClientListResponse>) {
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Debug] Serializing ClientListResponse" << std::endl;
      #endif
      payload["response_type"] = "client_list";
      payload["clients"] = json::array();
      for (const auto& client : r.clients) {
        json client_json;
        to_json(client_json, client);
        payload["clients"].push_back(client_json);
      }
    }
    else if constexpr (std::is_same_v<T, SuccessResponse>) {
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Debug] Serializing SuccessResponse: message=" << r.message << std::endl;
      #endif
      payload["response_type"] = "success";
      payload["message"] = r.message;
    }
    else if constexpr (std::is_same_v<T, ErrorResponse>) {
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Debug] Serializing ErrorResponse: error=" << r.error_message << std::endl;
      #endif
      payload["response_type"] = "error";
      payload["error_message"] = r.error_message;
      payload["details"] = r.details;
    }
  }, resp);

  #ifdef VEIL_IPC_DEBUG
  std::cerr << "[IPC Debug] serialize_response result: " << payload.dump() << std::endl;
  #endif

  return payload;
}

std::optional<Command> deserialize_command(const json& payload) {
  if (!payload.contains("command_type")) {
    return std::nullopt;
  }

  std::string cmd_type = payload.at("command_type").get<std::string>();

  if (cmd_type == "connect") {
    ConnectCommand cmd;
    if (payload.contains("config")) {
      from_json(payload.at("config"), cmd.config);
    }
    return cmd;
  }
  else if (cmd_type == "disconnect") {
    return DisconnectCommand{};
  }
  else if (cmd_type == "get_status") {
    return GetStatusCommand{};
  }
  else if (cmd_type == "get_metrics") {
    return GetMetricsCommand{};
  }
  else if (cmd_type == "get_diagnostics") {
    return GetDiagnosticsCommand{};
  }
  else if (cmd_type == "update_config") {
    UpdateConfigCommand cmd;
    if (payload.contains("config")) {
      from_json(payload.at("config"), cmd.config);
    }
    return cmd;
  }
  else if (cmd_type == "export_diagnostics") {
    ExportDiagnosticsCommand cmd;
    if (payload.contains("export_path")) {
      payload.at("export_path").get_to(cmd.export_path);
    }
    return cmd;
  }
  else if (cmd_type == "get_client_list") {
    return GetClientListCommand{};
  }

  return std::nullopt;
}

std::optional<Event> deserialize_event(const json& payload) {
  if (!payload.contains("event_type")) {
    return std::nullopt;
  }

  std::string evt_type = payload.at("event_type").get<std::string>();

  if (evt_type == "status_update") {
    StatusUpdateEvent evt;
    if (payload.contains("status")) {
      from_json(payload.at("status"), evt.status);
    }
    return evt;
  }
  else if (evt_type == "metrics_update") {
    MetricsUpdateEvent evt;
    if (payload.contains("metrics")) {
      from_json(payload.at("metrics"), evt.metrics);
    }
    return evt;
  }
  else if (evt_type == "connection_state_change") {
    ConnectionStateChangeEvent evt;
    if (payload.contains("old_state")) {
      auto state_opt = connection_state_from_string(payload.at("old_state").get<std::string>());
      evt.old_state = state_opt.value_or(ConnectionState::kDisconnected);
    }
    if (payload.contains("new_state")) {
      auto state_opt = connection_state_from_string(payload.at("new_state").get<std::string>());
      evt.new_state = state_opt.value_or(ConnectionState::kDisconnected);
    }
    if (payload.contains("message")) {
      payload.at("message").get_to(evt.message);
    }
    return evt;
  }
  else if (evt_type == "error") {
    ErrorEvent evt;
    if (payload.contains("error_message")) {
      payload.at("error_message").get_to(evt.error_message);
    }
    if (payload.contains("details")) {
      payload.at("details").get_to(evt.details);
    }
    return evt;
  }
  else if (evt_type == "log") {
    LogEventData evt;
    if (payload.contains("event")) {
      from_json(payload.at("event"), evt.event);
    }
    return evt;
  }
  else if (evt_type == "heartbeat") {
    HeartbeatEvent evt;
    if (payload.contains("timestamp_ms")) {
      payload.at("timestamp_ms").get_to(evt.timestamp_ms);
    }
    return evt;
  }
  else if (evt_type == "client_list_update") {
    ClientListUpdateEvent evt;
    if (payload.contains("clients") && payload.at("clients").is_array()) {
      for (const auto& client_json : payload.at("clients")) {
        ClientSession session;
        from_json(client_json, session);
        evt.clients.push_back(session);
      }
    }
    return evt;
  }
  else if (evt_type == "server_status_update") {
    ServerStatusUpdateEvent evt;
    if (payload.contains("status")) {
      const auto& s = payload.at("status");
      if (s.contains("running")) s.at("running").get_to(evt.status.running);
      if (s.contains("listen_port")) s.at("listen_port").get_to(evt.status.listen_port);
      if (s.contains("listen_address")) s.at("listen_address").get_to(evt.status.listen_address);
      if (s.contains("active_clients")) s.at("active_clients").get_to(evt.status.active_clients);
      if (s.contains("max_clients")) s.at("max_clients").get_to(evt.status.max_clients);
      if (s.contains("uptime_sec")) s.at("uptime_sec").get_to(evt.status.uptime_sec);
      if (s.contains("total_packets_sent")) s.at("total_packets_sent").get_to(evt.status.total_packets_sent);
      if (s.contains("total_packets_received")) s.at("total_packets_received").get_to(evt.status.total_packets_received);
      if (s.contains("total_bytes_sent")) s.at("total_bytes_sent").get_to(evt.status.total_bytes_sent);
      if (s.contains("total_bytes_received")) s.at("total_bytes_received").get_to(evt.status.total_bytes_received);
    }
    return evt;
  }

  return std::nullopt;
}

std::optional<Response> deserialize_response(const json& payload) {
  if (!payload.contains("response_type")) {
    #ifdef VEIL_IPC_DEBUG
    std::cerr << "[IPC Debug] Response payload missing 'response_type' field!" << std::endl;
    #endif
    return std::nullopt;
  }

  std::string resp_type = payload.at("response_type").get<std::string>();
  #ifdef VEIL_IPC_DEBUG
  std::cerr << "[IPC Debug] Response type: " << resp_type << std::endl;
  #endif

  if (resp_type == "status") {
    StatusResponse resp;
    if (payload.contains("status")) {
      from_json(payload.at("status"), resp.status);
    }
    return resp;
  }
  else if (resp_type == "metrics") {
    MetricsResponse resp;
    if (payload.contains("metrics")) {
      from_json(payload.at("metrics"), resp.metrics);
    }
    return resp;
  }
  else if (resp_type == "diagnostics") {
    DiagnosticsResponse resp;
    if (payload.contains("diagnostics")) {
      from_json(payload.at("diagnostics"), resp.diagnostics);
    }
    return resp;
  }
  else if (resp_type == "client_list") {
    ClientListResponse resp;
    if (payload.contains("clients") && payload.at("clients").is_array()) {
      for (const auto& client_json : payload.at("clients")) {
        ClientSession session;
        from_json(client_json, session);
        resp.clients.push_back(session);
      }
    }
    return resp;
  }
  else if (resp_type == "success") {
    SuccessResponse resp;
    if (payload.contains("message")) {
      payload.at("message").get_to(resp.message);
    }
    return resp;
  }
  else if (resp_type == "error") {
    ErrorResponse resp;
    if (payload.contains("error_message")) {
      payload.at("error_message").get_to(resp.error_message);
    }
    if (payload.contains("details")) {
      payload.at("details").get_to(resp.details);
    }
    return resp;
  }

  return std::nullopt;
}

}  // namespace

// ============================================================================
// Message Serialization
// ============================================================================

std::string serialize_message(const Message& msg) {
  json j;

  // Set message type
  switch (msg.type) {
    case MessageType::kCommand: j["type"] = "command"; break;
    case MessageType::kEvent: j["type"] = "event"; break;
    case MessageType::kResponse: j["type"] = "response"; break;
    case MessageType::kError: j["type"] = "error"; break;
  }

  // Set request ID if present
  if (msg.id) {
    j["id"] = *msg.id;
  }

  #ifdef VEIL_IPC_DEBUG
  std::cerr << "[IPC Debug] serialize_message: msg.type=" << static_cast<int>(msg.type)
            << ", payload.index=" << msg.payload.index() << std::endl;
  std::cerr << "[IPC Debug] Payload holds Command=" << std::holds_alternative<Command>(msg.payload)
            << ", Event=" << std::holds_alternative<Event>(msg.payload)
            << ", Response=" << std::holds_alternative<Response>(msg.payload) << std::endl;
  #endif

  // Serialize payload based on type
  std::visit([&j](const auto& payload) {
    using T = std::decay_t<decltype(payload)>;

    if constexpr (std::is_same_v<T, Command>) {
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Debug] Serializing as Command" << std::endl;
      #endif
      j["payload"] = serialize_command(payload);
    }
    else if constexpr (std::is_same_v<T, Event>) {
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Debug] Serializing as Event" << std::endl;
      #endif
      j["payload"] = serialize_event(payload);
    }
    else if constexpr (std::is_same_v<T, Response>) {
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Debug] Serializing as Response" << std::endl;
      #endif
      j["payload"] = serialize_response(payload);
    }
  }, msg.payload);

  // Add newline delimiter for framing
  std::string result = j.dump() + "\n";

  #ifdef VEIL_IPC_DEBUG
  std::cerr << "[IPC Debug] Serialized message: " << result;
  #endif

  return result;
}

std::optional<Message> deserialize_message(const std::string& json_str) {
  try {
    auto j = json::parse(json_str);

    // Debug: Log the raw JSON being deserialized
    #ifdef VEIL_IPC_DEBUG
    std::cerr << "[IPC Debug] Deserializing JSON: " << json_str << std::endl;
    #endif

    Message msg;

    // Parse message type
    auto type_str = j.at("type").get<std::string>();
    if (type_str == "command") {
      msg.type = MessageType::kCommand;
    } else if (type_str == "event") {
      msg.type = MessageType::kEvent;
    } else if (type_str == "response") {
      msg.type = MessageType::kResponse;
    } else if (type_str == "error") {
      msg.type = MessageType::kError;
    } else {
      return std::nullopt;
    }

    // Parse request ID if present
    if (j.contains("id")) {
      msg.id = j.at("id").get<std::uint64_t>();
    }

    // Parse payload based on message type
    if (!j.contains("payload")) {
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Debug] No payload in message!" << std::endl;
      #endif
      return std::nullopt;
    }

    const auto& payload = j.at("payload");
    #ifdef VEIL_IPC_DEBUG
    std::cerr << "[IPC Debug] Payload: " << payload.dump() << std::endl;
    #endif

    switch (msg.type) {
      case MessageType::kCommand: {
        auto cmd_opt = deserialize_command(payload);
        if (!cmd_opt) {
          return std::nullopt;
        }
        msg.payload = *cmd_opt;
        break;
      }
      case MessageType::kEvent: {
        auto evt_opt = deserialize_event(payload);
        if (!evt_opt) {
          return std::nullopt;
        }
        msg.payload = *evt_opt;
        break;
      }
      case MessageType::kResponse: {
        auto resp_opt = deserialize_response(payload);
        if (!resp_opt) {
          return std::nullopt;
        }
        msg.payload = *resp_opt;
        break;
      }
      case MessageType::kError: {
        // Error messages typically use ErrorResponse format
        auto resp_opt = deserialize_response(payload);
        if (!resp_opt) {
          return std::nullopt;
        }
        msg.payload = *resp_opt;
        break;
      }
    }

    return msg;
  } catch (const json::exception&) {
    return std::nullopt;
  }
}

}  // namespace veil::ipc
