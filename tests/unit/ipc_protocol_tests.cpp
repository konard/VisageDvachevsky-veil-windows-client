#include <gtest/gtest.h>

#include "common/ipc/ipc_protocol.h"

namespace veil::ipc::test {

// ============================================================================
// Test Fixture
// ============================================================================

class IPCProtocolTest : public ::testing::Test {
 protected:
  // Helper to verify Message round-trip
  template<typename T>
  void VerifyMessageRoundTrip(const Message& original) {
    // Serialize
    std::string serialized = serialize_message(original);
    EXPECT_FALSE(serialized.empty());

    // Deserialize
    auto deserialized_opt = deserialize_message(serialized);
    ASSERT_TRUE(deserialized_opt.has_value());

    const auto& deserialized = *deserialized_opt;

    // Verify type matches
    EXPECT_EQ(deserialized.type, original.type);

    // Verify ID matches
    EXPECT_EQ(deserialized.id, original.id);

    // Verify payload type matches
    EXPECT_EQ(deserialized.payload.index(), original.payload.index());
  }
};

// ============================================================================
// ConnectionState String Conversion Tests
// ============================================================================

TEST_F(IPCProtocolTest, ConnectionState_ToString_AllStates) {
  EXPECT_STREQ(connection_state_to_string(ConnectionState::kDisconnected), "disconnected");
  EXPECT_STREQ(connection_state_to_string(ConnectionState::kConnecting), "connecting");
  EXPECT_STREQ(connection_state_to_string(ConnectionState::kConnected), "connected");
  EXPECT_STREQ(connection_state_to_string(ConnectionState::kReconnecting), "reconnecting");
  EXPECT_STREQ(connection_state_to_string(ConnectionState::kError), "error");
}

TEST_F(IPCProtocolTest, ConnectionState_FromString_AllStates) {
  EXPECT_EQ(connection_state_from_string("disconnected"), ConnectionState::kDisconnected);
  EXPECT_EQ(connection_state_from_string("connecting"), ConnectionState::kConnecting);
  EXPECT_EQ(connection_state_from_string("connected"), ConnectionState::kConnected);
  EXPECT_EQ(connection_state_from_string("reconnecting"), ConnectionState::kReconnecting);
  EXPECT_EQ(connection_state_from_string("error"), ConnectionState::kError);
}

TEST_F(IPCProtocolTest, ConnectionState_FromString_InvalidReturnsNullopt) {
  EXPECT_FALSE(connection_state_from_string("invalid").has_value());
  EXPECT_FALSE(connection_state_from_string("").has_value());
  EXPECT_FALSE(connection_state_from_string("CONNECTED").has_value());
}

TEST_F(IPCProtocolTest, ConnectionState_RoundTrip) {
  auto states = {
    ConnectionState::kDisconnected,
    ConnectionState::kConnecting,
    ConnectionState::kConnected,
    ConnectionState::kReconnecting,
    ConnectionState::kError
  };

  for (auto state : states) {
    auto str = connection_state_to_string(state);
    auto parsed = connection_state_from_string(str);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, state);
  }
}

// ============================================================================
// Command Tests
// ============================================================================

TEST_F(IPCProtocolTest, ConnectCommand_Serialization) {
  Message msg;
  msg.type = MessageType::kCommand;
  msg.id = 123;

  ConnectCommand cmd;
  cmd.config.server_address = "192.168.1.1";
  cmd.config.server_port = 4433;
  cmd.config.enable_obfuscation = true;
  cmd.config.auto_reconnect = true;
  cmd.config.reconnect_interval_sec = 10;
  cmd.config.max_reconnect_attempts = 5;
  cmd.config.route_all_traffic = true;
  cmd.config.custom_routes = {"10.0.0.0/8", "172.16.0.0/12"};
  cmd.config.key_file = "/path/to/key.pem";
  cmd.config.obfuscation_seed_file = "/path/to/seed";
  cmd.config.tun_device_name = "veil0";
  cmd.config.tun_ip_address = "10.8.0.2";
  cmd.config.tun_netmask = "255.255.255.0";
  cmd.config.tun_mtu = 1400;

  msg.payload = cmd;

  VerifyMessageRoundTrip<Command>(msg);

  // Verify specific fields
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_cmd = std::get<Command>(deserialized->payload);
  auto& connect_cmd = std::get<ConnectCommand>(deserialized_cmd);

  EXPECT_EQ(connect_cmd.config.server_address, "192.168.1.1");
  EXPECT_EQ(connect_cmd.config.server_port, 4433);
  EXPECT_TRUE(connect_cmd.config.enable_obfuscation);
  EXPECT_EQ(connect_cmd.config.custom_routes.size(), 2);
  EXPECT_EQ(connect_cmd.config.tun_mtu, 1400);
}

TEST_F(IPCProtocolTest, DisconnectCommand_Serialization) {
  Message msg;
  msg.type = MessageType::kCommand;
  msg.id = 456;
  msg.payload = Command{DisconnectCommand{}};

  VerifyMessageRoundTrip<Command>(msg);
}

TEST_F(IPCProtocolTest, GetStatusCommand_Serialization) {
  Message msg;
  msg.type = MessageType::kCommand;
  msg.id = 789;
  msg.payload = Command{GetStatusCommand{}};

  VerifyMessageRoundTrip<Command>(msg);
}

TEST_F(IPCProtocolTest, GetMetricsCommand_Serialization) {
  Message msg;
  msg.type = MessageType::kCommand;
  msg.id = 1011;
  msg.payload = Command{GetMetricsCommand{}};

  VerifyMessageRoundTrip<Command>(msg);
}

TEST_F(IPCProtocolTest, GetDiagnosticsCommand_Serialization) {
  Message msg;
  msg.type = MessageType::kCommand;
  msg.id = 1213;
  msg.payload = Command{GetDiagnosticsCommand{}};

  VerifyMessageRoundTrip<Command>(msg);
}

TEST_F(IPCProtocolTest, UpdateConfigCommand_Serialization) {
  Message msg;
  msg.type = MessageType::kCommand;
  msg.id = 1415;

  UpdateConfigCommand cmd;
  cmd.config.server_address = "example.com";
  cmd.config.server_port = 8443;
  cmd.config.enable_obfuscation = false;
  cmd.config.auto_reconnect = false;

  msg.payload = cmd;

  VerifyMessageRoundTrip<Command>(msg);
}

TEST_F(IPCProtocolTest, ExportDiagnosticsCommand_Serialization) {
  Message msg;
  msg.type = MessageType::kCommand;
  msg.id = 1617;

  ExportDiagnosticsCommand cmd;
  cmd.export_path = "/tmp/diagnostics.json";

  msg.payload = cmd;

  VerifyMessageRoundTrip<Command>(msg);

  // Verify export path
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_cmd = std::get<Command>(deserialized->payload);
  auto& export_cmd = std::get<ExportDiagnosticsCommand>(deserialized_cmd);

  EXPECT_EQ(export_cmd.export_path, "/tmp/diagnostics.json");
}

TEST_F(IPCProtocolTest, GetClientListCommand_Serialization) {
  Message msg;
  msg.type = MessageType::kCommand;
  msg.id = 1819;
  msg.payload = Command{GetClientListCommand{}};

  VerifyMessageRoundTrip<Command>(msg);
}

// ============================================================================
// Event Tests
// ============================================================================

TEST_F(IPCProtocolTest, StatusUpdateEvent_Serialization) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = 2021;

  StatusUpdateEvent evt;
  evt.status.state = ConnectionState::kConnected;
  evt.status.session_id = "session-12345";
  evt.status.server_address = "192.168.1.1";
  evt.status.server_port = 4433;
  evt.status.uptime_sec = 3600;
  evt.status.error_message = "";
  evt.status.reconnect_attempt = 0;

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);

  // Verify specific fields
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_evt = std::get<Event>(deserialized->payload);
  auto& status_evt = std::get<StatusUpdateEvent>(deserialized_evt);

  EXPECT_EQ(status_evt.status.state, ConnectionState::kConnected);
  EXPECT_EQ(status_evt.status.session_id, "session-12345");
  EXPECT_EQ(status_evt.status.uptime_sec, 3600);
}

TEST_F(IPCProtocolTest, MetricsUpdateEvent_Serialization) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = 2223;

  MetricsUpdateEvent evt;
  evt.metrics.latency_ms = 50;
  evt.metrics.tx_bytes_per_sec = 1024000;
  evt.metrics.rx_bytes_per_sec = 2048000;
  evt.metrics.total_tx_bytes = 1024ULL * 1024ULL * 100ULL;
  evt.metrics.total_rx_bytes = 1024ULL * 1024ULL * 200ULL;

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);

  // Verify metrics
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_evt = std::get<Event>(deserialized->payload);
  auto& metrics_evt = std::get<MetricsUpdateEvent>(deserialized_evt);

  EXPECT_EQ(metrics_evt.metrics.latency_ms, 50);
  EXPECT_EQ(metrics_evt.metrics.total_tx_bytes, 1024 * 1024 * 100);
}

TEST_F(IPCProtocolTest, ConnectionStateChangeEvent_Serialization) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = 2425;

  ConnectionStateChangeEvent evt;
  evt.old_state = ConnectionState::kConnecting;
  evt.new_state = ConnectionState::kConnected;
  evt.message = "Successfully connected to server";

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);

  // Verify state change
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_evt = std::get<Event>(deserialized->payload);
  auto& state_evt = std::get<ConnectionStateChangeEvent>(deserialized_evt);

  EXPECT_EQ(state_evt.old_state, ConnectionState::kConnecting);
  EXPECT_EQ(state_evt.new_state, ConnectionState::kConnected);
  EXPECT_EQ(state_evt.message, "Successfully connected to server");
}

TEST_F(IPCProtocolTest, ErrorEvent_Serialization) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = 2627;

  ErrorEvent evt;
  evt.error_message = "Connection failed";
  evt.details = "Timeout after 30 seconds";

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);

  // Verify error details
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_evt = std::get<Event>(deserialized->payload);
  auto& error_evt = std::get<ErrorEvent>(deserialized_evt);

  EXPECT_EQ(error_evt.error_message, "Connection failed");
  EXPECT_EQ(error_evt.details, "Timeout after 30 seconds");
}

TEST_F(IPCProtocolTest, LogEventData_Serialization) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = 2829;

  LogEventData evt;
  evt.event.timestamp_ms = 1234567890;
  evt.event.level = "info";
  evt.event.message = "Connection established";

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);

  // Verify log event
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_evt = std::get<Event>(deserialized->payload);
  auto& log_evt = std::get<LogEventData>(deserialized_evt);

  EXPECT_EQ(log_evt.event.timestamp_ms, 1234567890);
  EXPECT_EQ(log_evt.event.level, "info");
  EXPECT_EQ(log_evt.event.message, "Connection established");
}

TEST_F(IPCProtocolTest, HeartbeatEvent_Serialization) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = 2930;

  HeartbeatEvent evt;
  evt.timestamp_ms = 1706745600000;  // Example timestamp in ms

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);

  // Verify specific fields
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_evt = std::get<Event>(deserialized->payload);
  auto& heartbeat_evt = std::get<HeartbeatEvent>(deserialized_evt);

  EXPECT_EQ(heartbeat_evt.timestamp_ms, 1706745600000);
}

TEST_F(IPCProtocolTest, HeartbeatEvent_ZeroTimestamp) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = 2931;

  HeartbeatEvent evt;
  evt.timestamp_ms = 0;

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);

  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_evt = std::get<Event>(deserialized->payload);
  auto& heartbeat_evt = std::get<HeartbeatEvent>(deserialized_evt);

  EXPECT_EQ(heartbeat_evt.timestamp_ms, 0);
}

TEST_F(IPCProtocolTest, HeartbeatEvent_MaxTimestamp) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = 2932;

  HeartbeatEvent evt;
  evt.timestamp_ms = std::numeric_limits<std::uint64_t>::max();

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);
}

TEST_F(IPCProtocolTest, HeartbeatEvent_WithoutID) {
  Message msg;
  msg.type = MessageType::kEvent;
  // No ID set

  HeartbeatEvent evt;
  evt.timestamp_ms = 9876543210000;

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);

  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_FALSE(deserialized->id.has_value());

  auto& deserialized_evt = std::get<Event>(deserialized->payload);
  auto& heartbeat_evt = std::get<HeartbeatEvent>(deserialized_evt);
  EXPECT_EQ(heartbeat_evt.timestamp_ms, 9876543210000);
}

TEST_F(IPCProtocolTest, HeartbeatEvent_DeserializeFromJSON) {
  // Test deserializing a heartbeat event from raw JSON
  std::string json_str = R"({
    "type": "event",
    "id": 2933,
    "payload": {
      "event_type": "heartbeat",
      "timestamp_ms": 1706745600000
    }
  })";

  auto msg_opt = deserialize_message(json_str);
  ASSERT_TRUE(msg_opt.has_value());
  EXPECT_EQ(msg_opt->type, MessageType::kEvent);
  EXPECT_EQ(msg_opt->id.value(), 2933);

  auto& evt = std::get<Event>(msg_opt->payload);
  auto& heartbeat_evt = std::get<HeartbeatEvent>(evt);
  EXPECT_EQ(heartbeat_evt.timestamp_ms, 1706745600000);
}

TEST_F(IPCProtocolTest, HeartbeatEvent_DeserializeMissingTimestamp) {
  // Test deserializing heartbeat with missing timestamp_ms field
  std::string json_str = R"({
    "type": "event",
    "id": 2934,
    "payload": {
      "event_type": "heartbeat"
    }
  })";

  auto msg_opt = deserialize_message(json_str);
  ASSERT_TRUE(msg_opt.has_value());

  auto& evt = std::get<Event>(msg_opt->payload);
  auto& heartbeat_evt = std::get<HeartbeatEvent>(evt);
  // Default value should be 0
  EXPECT_EQ(heartbeat_evt.timestamp_ms, 0);
}

TEST_F(IPCProtocolTest, ClientListUpdateEvent_Serialization) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = 3031;

  ClientListUpdateEvent evt;

  ClientSession session1;
  session1.session_id = 1;
  session1.tunnel_ip = "10.8.0.2";
  session1.endpoint_host = "192.168.1.100";
  session1.endpoint_port = 12345;
  session1.uptime_sec = 600;
  session1.packets_sent = 1000;
  session1.packets_received = 900;
  session1.bytes_sent = 50000;
  session1.bytes_received = 45000;
  session1.last_activity_sec = 5;

  ClientSession session2;
  session2.session_id = 2;
  session2.tunnel_ip = "10.8.0.3";
  session2.endpoint_host = "192.168.1.101";
  session2.endpoint_port = 54321;

  evt.clients = {session1, session2};

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);

  // Verify client list
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_evt = std::get<Event>(deserialized->payload);
  auto& client_evt = std::get<ClientListUpdateEvent>(deserialized_evt);

  EXPECT_EQ(client_evt.clients.size(), 2);
  EXPECT_EQ(client_evt.clients[0].session_id, 1);
  EXPECT_EQ(client_evt.clients[0].tunnel_ip, "10.8.0.2");
  EXPECT_EQ(client_evt.clients[1].session_id, 2);
}

TEST_F(IPCProtocolTest, ServerStatusUpdateEvent_Serialization) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = 3233;

  ServerStatusUpdateEvent evt;
  evt.status.running = true;
  evt.status.listen_port = 4433;
  evt.status.listen_address = "0.0.0.0";
  evt.status.active_clients = 5;
  evt.status.max_clients = 100;
  evt.status.uptime_sec = 86400;
  evt.status.total_packets_sent = 10000;
  evt.status.total_packets_received = 9500;
  evt.status.total_bytes_sent = 1024000;
  evt.status.total_bytes_received = 900000;

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);

  // Verify server status
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_evt = std::get<Event>(deserialized->payload);
  auto& server_evt = std::get<ServerStatusUpdateEvent>(deserialized_evt);

  EXPECT_TRUE(server_evt.status.running);
  EXPECT_EQ(server_evt.status.listen_port, 4433);
  EXPECT_EQ(server_evt.status.active_clients, 5);
  EXPECT_EQ(server_evt.status.uptime_sec, 86400);
}

// ============================================================================
// Response Tests
// ============================================================================

TEST_F(IPCProtocolTest, StatusResponse_Serialization) {
  Message msg;
  msg.type = MessageType::kResponse;
  msg.id = 3435;

  StatusResponse resp;
  resp.status.state = ConnectionState::kConnected;
  resp.status.session_id = "resp-session";
  resp.status.server_address = "server.example.com";
  resp.status.server_port = 443;
  resp.status.uptime_sec = 7200;
  resp.status.error_message = "";
  resp.status.reconnect_attempt = 0;

  msg.payload = Response{resp};

  VerifyMessageRoundTrip<Response>(msg);
}

TEST_F(IPCProtocolTest, MetricsResponse_Serialization) {
  Message msg;
  msg.type = MessageType::kResponse;
  msg.id = 3637;

  MetricsResponse resp;
  resp.metrics.latency_ms = 25;
  resp.metrics.tx_bytes_per_sec = 500000;
  resp.metrics.rx_bytes_per_sec = 1000000;
  resp.metrics.total_tx_bytes = 1000000000;
  resp.metrics.total_rx_bytes = 2000000000;

  msg.payload = Response{resp};

  VerifyMessageRoundTrip<Response>(msg);
}

TEST_F(IPCProtocolTest, DiagnosticsResponse_Serialization) {
  Message msg;
  msg.type = MessageType::kResponse;
  msg.id = 3839;

  DiagnosticsResponse resp;

  // Protocol metrics
  resp.diagnostics.protocol.send_sequence = 1000;
  resp.diagnostics.protocol.recv_sequence = 995;
  resp.diagnostics.protocol.packets_sent = 1500;
  resp.diagnostics.protocol.packets_received = 1450;
  resp.diagnostics.protocol.packets_lost = 10;
  resp.diagnostics.protocol.packets_retransmitted = 5;
  resp.diagnostics.protocol.loss_percentage = 0.67;

  // Reassembly stats
  resp.diagnostics.reassembly.fragments_received = 500;
  resp.diagnostics.reassembly.messages_reassembled = 100;
  resp.diagnostics.reassembly.fragments_pending = 2;
  resp.diagnostics.reassembly.reassembly_timeouts = 1;

  // Obfuscation profile
  resp.diagnostics.obfuscation.padding_enabled = true;
  resp.diagnostics.obfuscation.current_padding_size = 128;
  resp.diagnostics.obfuscation.timing_jitter_model = "exponential";
  resp.diagnostics.obfuscation.timing_jitter_param = 0.5;
  resp.diagnostics.obfuscation.heartbeat_mode = "adaptive";
  resp.diagnostics.obfuscation.last_heartbeat_sec = 1.5;
  resp.diagnostics.obfuscation.active_dpi_mode = "IoT";

  // Recent events
  LogEvent event1;
  event1.timestamp_ms = 1000;
  event1.level = "info";
  event1.message = "Event 1";

  LogEvent event2;
  event2.timestamp_ms = 2000;
  event2.level = "warning";
  event2.message = "Event 2";

  resp.diagnostics.recent_events = {event1, event2};

  msg.payload = Response{resp};

  VerifyMessageRoundTrip<Response>(msg);

  // Verify diagnostics details
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_resp = std::get<Response>(deserialized->payload);
  auto& diag_resp = std::get<DiagnosticsResponse>(deserialized_resp);

  EXPECT_EQ(diag_resp.diagnostics.protocol.packets_sent, 1500);
  EXPECT_EQ(diag_resp.diagnostics.reassembly.fragments_received, 500);
  EXPECT_TRUE(diag_resp.diagnostics.obfuscation.padding_enabled);
  EXPECT_EQ(diag_resp.diagnostics.obfuscation.active_dpi_mode, "IoT");
  EXPECT_EQ(diag_resp.diagnostics.recent_events.size(), 2);
  EXPECT_EQ(diag_resp.diagnostics.recent_events[0].level, "info");
}

TEST_F(IPCProtocolTest, ClientListResponse_Serialization) {
  Message msg;
  msg.type = MessageType::kResponse;
  msg.id = 4041;

  ClientListResponse resp;

  ClientSession session;
  session.session_id = 42;
  session.tunnel_ip = "10.8.0.10";
  session.endpoint_host = "client.example.com";
  session.endpoint_port = 9999;
  session.uptime_sec = 1800;

  resp.clients = {session};

  msg.payload = Response{resp};

  VerifyMessageRoundTrip<Response>(msg);
}

TEST_F(IPCProtocolTest, SuccessResponse_Serialization) {
  Message msg;
  msg.type = MessageType::kResponse;
  msg.id = 4243;

  SuccessResponse resp;
  resp.message = "Operation completed successfully";

  msg.payload = Response{resp};

  VerifyMessageRoundTrip<Response>(msg);

  // Verify message
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_resp = std::get<Response>(deserialized->payload);
  auto& success_resp = std::get<SuccessResponse>(deserialized_resp);

  EXPECT_EQ(success_resp.message, "Operation completed successfully");
}

TEST_F(IPCProtocolTest, ErrorResponse_Serialization) {
  Message msg;
  msg.type = MessageType::kResponse;
  msg.id = 4445;

  ErrorResponse resp;
  resp.error_message = "Invalid configuration";
  resp.details = "Missing required field: server_address";

  msg.payload = Response{resp};

  VerifyMessageRoundTrip<Response>(msg);

  // Verify error details
  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_resp = std::get<Response>(deserialized->payload);
  auto& error_resp = std::get<ErrorResponse>(deserialized_resp);

  EXPECT_EQ(error_resp.error_message, "Invalid configuration");
  EXPECT_EQ(error_resp.details, "Missing required field: server_address");
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(IPCProtocolTest, EmptyStrings) {
  Message msg;
  msg.type = MessageType::kCommand;
  msg.id = 5001;

  ConnectCommand cmd;
  cmd.config.server_address = "";
  cmd.config.server_port = 0;
  cmd.config.key_file = "";
  cmd.config.custom_routes = {};

  msg.payload = cmd;

  VerifyMessageRoundTrip<Command>(msg);

  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_cmd = std::get<Command>(deserialized->payload);
  auto& connect_cmd = std::get<ConnectCommand>(deserialized_cmd);

  EXPECT_EQ(connect_cmd.config.server_address, "");
  EXPECT_EQ(connect_cmd.config.server_port, 0);
  EXPECT_TRUE(connect_cmd.config.custom_routes.empty());
}

TEST_F(IPCProtocolTest, ZeroValues) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = 5002;

  MetricsUpdateEvent evt;
  evt.metrics.latency_ms = 0;
  evt.metrics.tx_bytes_per_sec = 0;
  evt.metrics.rx_bytes_per_sec = 0;
  evt.metrics.total_tx_bytes = 0;
  evt.metrics.total_rx_bytes = 0;

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);
}

TEST_F(IPCProtocolTest, MaximumFieldValues) {
  Message msg;
  msg.type = MessageType::kEvent;
  msg.id = std::numeric_limits<std::uint64_t>::max();

  MetricsUpdateEvent evt;
  evt.metrics.latency_ms = std::numeric_limits<std::uint32_t>::max();
  evt.metrics.tx_bytes_per_sec = std::numeric_limits<std::uint64_t>::max();
  evt.metrics.rx_bytes_per_sec = std::numeric_limits<std::uint64_t>::max();
  evt.metrics.total_tx_bytes = std::numeric_limits<std::uint64_t>::max();
  evt.metrics.total_rx_bytes = std::numeric_limits<std::uint64_t>::max();

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);
}

TEST_F(IPCProtocolTest, MessageWithoutID) {
  Message msg;
  msg.type = MessageType::kEvent;
  // No ID set (std::nullopt)

  StatusUpdateEvent evt;
  evt.status.state = ConnectionState::kDisconnected;
  evt.status.session_id = "";
  evt.status.server_address = "";
  evt.status.server_port = 0;

  msg.payload = Event{evt};

  VerifyMessageRoundTrip<Event>(msg);

  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_FALSE(deserialized->id.has_value());
}

TEST_F(IPCProtocolTest, MissingOptionalFields_ConnectionConfig) {
  // Create JSON with only required fields
  std::string json_str = R"({
    "type": "command",
    "id": 6001,
    "payload": {
      "command_type": "connect",
      "config": {
        "server_address": "test.com",
        "server_port": 443,
        "enable_obfuscation": true,
        "auto_reconnect": true,
        "reconnect_interval_sec": 5,
        "max_reconnect_attempts": 3,
        "route_all_traffic": true,
        "custom_routes": []
      }
    }
  })";

  auto msg_opt = deserialize_message(json_str);
  ASSERT_TRUE(msg_opt.has_value());

  auto& cmd = std::get<Command>(msg_opt->payload);
  auto& connect_cmd = std::get<ConnectCommand>(cmd);

  // Verify defaults are used for missing optional fields
  EXPECT_EQ(connect_cmd.config.tun_device_name, "veil0");
  EXPECT_EQ(connect_cmd.config.tun_ip_address, "10.8.0.2");
  EXPECT_EQ(connect_cmd.config.tun_netmask, "255.255.255.0");
  EXPECT_EQ(connect_cmd.config.tun_mtu, 1400);
}

TEST_F(IPCProtocolTest, EmptyEventList) {
  Message msg;
  msg.type = MessageType::kResponse;
  msg.id = 6002;

  DiagnosticsResponse resp;
  resp.diagnostics.recent_events = {};  // Empty list

  msg.payload = Response{resp};

  VerifyMessageRoundTrip<Response>(msg);

  auto serialized = serialize_message(msg);
  auto deserialized = deserialize_message(serialized);
  ASSERT_TRUE(deserialized.has_value());

  auto& deserialized_resp = std::get<Response>(deserialized->payload);
  auto& diag_resp = std::get<DiagnosticsResponse>(deserialized_resp);

  EXPECT_TRUE(diag_resp.diagnostics.recent_events.empty());
}

TEST_F(IPCProtocolTest, EmptyClientList) {
  Message msg;
  msg.type = MessageType::kResponse;
  msg.id = 6003;

  ClientListResponse resp;
  resp.clients = {};  // Empty list

  msg.payload = Response{resp};

  VerifyMessageRoundTrip<Response>(msg);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(IPCProtocolTest, InvalidJSON) {
  std::string invalid_json = "{ this is not valid json }";
  auto msg_opt = deserialize_message(invalid_json);
  EXPECT_FALSE(msg_opt.has_value());
}

TEST_F(IPCProtocolTest, MissingTypeField) {
  std::string json_str = R"({
    "id": 7001,
    "payload": {
      "command_type": "disconnect"
    }
  })";

  auto msg_opt = deserialize_message(json_str);
  EXPECT_FALSE(msg_opt.has_value());
}

TEST_F(IPCProtocolTest, MissingPayloadField) {
  std::string json_str = R"({
    "type": "command",
    "id": 7002
  })";

  auto msg_opt = deserialize_message(json_str);
  EXPECT_FALSE(msg_opt.has_value());
}

TEST_F(IPCProtocolTest, UnknownMessageType) {
  std::string json_str = R"({
    "type": "unknown_type",
    "id": 7003,
    "payload": {}
  })";

  auto msg_opt = deserialize_message(json_str);
  EXPECT_FALSE(msg_opt.has_value());
}

TEST_F(IPCProtocolTest, UnknownCommandType) {
  std::string json_str = R"({
    "type": "command",
    "id": 7004,
    "payload": {
      "command_type": "unknown_command"
    }
  })";

  auto msg_opt = deserialize_message(json_str);
  EXPECT_FALSE(msg_opt.has_value());
}

TEST_F(IPCProtocolTest, UnknownEventType) {
  std::string json_str = R"({
    "type": "event",
    "id": 7005,
    "payload": {
      "event_type": "unknown_event"
    }
  })";

  auto msg_opt = deserialize_message(json_str);
  EXPECT_FALSE(msg_opt.has_value());
}

TEST_F(IPCProtocolTest, UnknownResponseType) {
  std::string json_str = R"({
    "type": "response",
    "id": 7006,
    "payload": {
      "response_type": "unknown_response"
    }
  })";

  auto msg_opt = deserialize_message(json_str);
  EXPECT_FALSE(msg_opt.has_value());
}

TEST_F(IPCProtocolTest, MissingRequiredField_ConnectionConfig) {
  std::string json_str = R"({
    "type": "command",
    "id": 7007,
    "payload": {
      "command_type": "connect",
      "config": {
        "server_port": 443
      }
    }
  })";

  // Missing server_address should cause parse failure
  auto msg_opt = deserialize_message(json_str);
  EXPECT_FALSE(msg_opt.has_value());
}

TEST_F(IPCProtocolTest, InvalidStateString) {
  std::string json_str = R"({
    "type": "event",
    "id": 7008,
    "payload": {
      "event_type": "connection_state_change",
      "old_state": "invalid_state",
      "new_state": "connected",
      "message": "test"
    }
  })";

  auto msg_opt = deserialize_message(json_str);
  ASSERT_TRUE(msg_opt.has_value());

  // Invalid state should default to kDisconnected
  auto& evt = std::get<Event>(msg_opt->payload);
  auto& state_evt = std::get<ConnectionStateChangeEvent>(evt);

  EXPECT_EQ(state_evt.old_state, ConnectionState::kDisconnected);
  EXPECT_EQ(state_evt.new_state, ConnectionState::kConnected);
}

TEST_F(IPCProtocolTest, MalformedJSON_ExtraComma) {
  std::string json_str = R"({
    "type": "command",
    "id": 7009,
    "payload": {
      "command_type": "disconnect",
    }
  })";

  auto msg_opt = deserialize_message(json_str);
  EXPECT_FALSE(msg_opt.has_value());
}

TEST_F(IPCProtocolTest, WrongTypeForField) {
  std::string json_str = R"({
    "type": "command",
    "id": "not_a_number",
    "payload": {
      "command_type": "disconnect"
    }
  })";

  auto msg_opt = deserialize_message(json_str);
  EXPECT_FALSE(msg_opt.has_value());
}

// ============================================================================
// Comprehensive Round-Trip Test
// ============================================================================

TEST_F(IPCProtocolTest, AllMessageTypes_RoundTrip) {
  // Test that we can serialize and deserialize all message types

  std::vector<Message> messages;

  // Commands
  messages.push_back(Message{MessageType::kCommand, 1, Command{ConnectCommand{}}});
  messages.push_back(Message{MessageType::kCommand, 2, Command{DisconnectCommand{}}});
  messages.push_back(Message{MessageType::kCommand, 3, Command{GetStatusCommand{}}});
  messages.push_back(Message{MessageType::kCommand, 4, Command{GetMetricsCommand{}}});
  messages.push_back(Message{MessageType::kCommand, 5, Command{GetDiagnosticsCommand{}}});
  messages.push_back(Message{MessageType::kCommand, 6, Command{UpdateConfigCommand{}}});
  messages.push_back(Message{MessageType::kCommand, 7, Command{ExportDiagnosticsCommand{}}});
  messages.push_back(Message{MessageType::kCommand, 8, Command{GetClientListCommand{}}});

  // Events
  messages.push_back(Message{MessageType::kEvent, 9, Event{StatusUpdateEvent{}}});
  messages.push_back(Message{MessageType::kEvent, 10, Event{MetricsUpdateEvent{}}});
  messages.push_back(Message{MessageType::kEvent, 11, Event{ConnectionStateChangeEvent{}}});
  messages.push_back(Message{MessageType::kEvent, 12, Event{ErrorEvent{}}});
  messages.push_back(Message{MessageType::kEvent, 13, Event{LogEventData{}}});
  messages.push_back(Message{MessageType::kEvent, 14, Event{HeartbeatEvent{}}});
  messages.push_back(Message{MessageType::kEvent, 15, Event{ClientListUpdateEvent{}}});
  messages.push_back(Message{MessageType::kEvent, 16, Event{ServerStatusUpdateEvent{}}});

  // Responses
  messages.push_back(Message{MessageType::kResponse, 17, Response{StatusResponse{}}});
  messages.push_back(Message{MessageType::kResponse, 18, Response{MetricsResponse{}}});
  messages.push_back(Message{MessageType::kResponse, 19, Response{DiagnosticsResponse{}}});
  messages.push_back(Message{MessageType::kResponse, 20, Response{ClientListResponse{}}});
  messages.push_back(Message{MessageType::kResponse, 21, Response{SuccessResponse{}}});
  messages.push_back(Message{MessageType::kResponse, 22, Response{ErrorResponse{}}});

  for (const auto& msg : messages) {
    auto serialized = serialize_message(msg);
    EXPECT_FALSE(serialized.empty());

    auto deserialized = deserialize_message(serialized);
    ASSERT_TRUE(deserialized.has_value()) << "Failed to deserialize message with ID " << msg.id.value_or(0);

    EXPECT_EQ(deserialized->type, msg.type);
    EXPECT_EQ(deserialized->id, msg.id);
  }
}

}  // namespace veil::ipc::test
