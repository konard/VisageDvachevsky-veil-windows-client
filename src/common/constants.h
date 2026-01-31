#pragma once

// ============================================================================
// VEIL VPN Global Constants
// ============================================================================
// Centralized constants for IPC communication, service management, and
// Windows platform integration. This header provides a single source of truth
// for names and identifiers used across multiple components.

namespace veil {

// Windows Service Configuration
// These constants define the Windows service identity and metadata.
constexpr const char* kServiceName = "VeilVPN";
constexpr const char* kServiceDisplayName = "VEIL VPN Service";
constexpr const char* kServiceDescription =
    "Provides secure VPN connectivity through the VEIL protocol";

// Windows Event Synchronization
// Named event used to signal that the IPC server is ready to accept connections.
// The "Global\\" prefix makes this event visible across all user sessions.
constexpr const char* kServiceReadyEventName = "Global\\VEIL_SERVICE_READY";

// IPC Named Pipe Paths (Windows)
// These named pipes facilitate communication between the service and GUI clients.
constexpr const char* kIpcClientPipeName = R"(\\.\pipe\veil-client)";
constexpr const char* kIpcServerPipeName = R"(\\.\pipe\veil-server)";

// IPC Socket Paths (Unix/Linux)
// Unix domain sockets used for IPC on non-Windows platforms.
constexpr const char* kIpcClientSocketPath = "/tmp/veil-client.sock";
constexpr const char* kIpcServerSocketPath = "/tmp/veil-server.sock";

}  // namespace veil
