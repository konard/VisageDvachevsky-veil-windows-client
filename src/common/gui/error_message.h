#pragma once

#include <string>
#include <utility>

namespace veil::gui {

/// Error categories for user-facing error messages
enum class ErrorCategory {
  kNetwork,       // Connection failed, timeout, network unreachable
  kConfiguration, // Invalid settings, missing files, bad configuration
  kPermission,    // Admin rights needed, access denied
  kDaemon,        // Service not running, IPC connection failed
  kUnknown        // Unclassified errors
};

/// Structured error message with category and actionable guidance
struct ErrorMessage {
  ErrorCategory category;
  std::string title;           // Short error title (e.g., "Connection Timeout")
  std::string description;     // Detailed error description
  std::string action;          // Actionable guidance for the user
  std::string technical_details; // Optional technical details for support

  ErrorMessage()
      : category(ErrorCategory::kUnknown) {}

  // NOLINTNEXTLINE(performance-unnecessary-value-param)
  ErrorMessage(ErrorCategory cat, std::string t, std::string desc,
               std::string act, std::string details = "")  // NOLINT(performance-unnecessary-value-param)
      : category(cat), title(std::move(t)), description(std::move(desc)),
        action(std::move(act)), technical_details(std::move(details)) {}

  /// Get a user-friendly string representation
  std::string to_user_string() const {
    std::string result = title;
    if (!description.empty()) {
      result += "\n\n" + description;
    }
    if (!action.empty()) {
      result += "\n\n" + action;
    }
    return result;
  }

  /// Get full details including technical information
  std::string to_detailed_string() const {
    std::string result = to_user_string();
    if (!technical_details.empty()) {
      result += "\n\nTechnical Details:\n" + technical_details;
    }
    return result;
  }

  /// Get category name as string
  std::string category_name() const {
    switch (category) {
      case ErrorCategory::kNetwork:
        return "Network Error";
      case ErrorCategory::kConfiguration:
        return "Configuration Error";
      case ErrorCategory::kPermission:
        return "Permission Error";
      case ErrorCategory::kDaemon:
        return "Service Error";
      default:
        return "Error";
    }
  }
};

// ============================================================================
// Pre-defined Error Messages
// ============================================================================

namespace errors {

/// Connection timeout error
inline ErrorMessage connection_timeout() {
  return ErrorMessage(
      ErrorCategory::kDaemon,
      "Connection Timeout",
      "The connection attempt timed out after 30 seconds.",
      "Please ensure:\n"
      "• The VEIL service is running (check Windows Services)\n"
      "• Your firewall allows VEIL connections\n"
      "• The server address and port are correct in Settings");
}

/// Daemon not running error
inline ErrorMessage daemon_not_running() {
  return ErrorMessage(
      ErrorCategory::kDaemon,
      "Service Not Running",
      "Cannot connect to the VEIL daemon service.",
      "To start the service:\n"
      "• Run this application as Administrator, or\n"
      "• Open Windows Services (services.msc)\n"
      "• Find 'VEIL VPN Service' and click Start");
}

/// Network unreachable error
inline ErrorMessage network_unreachable() {
  return ErrorMessage(
      ErrorCategory::kNetwork,
      "Network Unreachable",
      "Cannot reach the VPN server at the configured address.",
      "Please check:\n"
      "• Your internet connection is active\n"
      "• The server address is correct in Settings\n"
      "• Your firewall allows outbound UDP traffic");
}

/// Configuration error - missing key file
inline ErrorMessage missing_key_file(const std::string& file_path) {
  return ErrorMessage(
      ErrorCategory::kConfiguration,
      "Pre-shared Key Not Found",
      "The configured pre-shared key file does not exist.",
      "To fix this issue:\n"
      "• Go to Settings > Cryptographic Settings\n"
      "• Select a valid pre-shared key file\n"
      "• Or request a new key file from your VPN administrator",
      "File path: " + file_path);
}

/// Configuration error - invalid server address
inline ErrorMessage invalid_server_address(const std::string& address) {
  return ErrorMessage(
      ErrorCategory::kConfiguration,
      "Invalid Server Address",
      "The configured server address is not valid.",
      "To fix this issue:\n"
      "• Go to Settings > Server Configuration\n"
      "• Enter a valid server address (e.g., vpn.example.com)\n"
      "• Ensure the port number is correct (default: 4433)",
      "Address: " + address);
}

/// Permission error - service installation
inline ErrorMessage permission_denied_service_install() {
  return ErrorMessage(
      ErrorCategory::kPermission,
      "Administrator Rights Required",
      "Installing the VEIL service requires administrator privileges.",
      "To install the service:\n"
      "• Close this application\n"
      "• Right-click the VEIL VPN icon\n"
      "• Select 'Run as Administrator'\n"
      "• Try connecting again");
}

/// Permission error - service start
inline ErrorMessage permission_denied_service_start() {
  return ErrorMessage(
      ErrorCategory::kPermission,
      "Administrator Rights Required",
      "Starting the VEIL service requires administrator privileges.",
      "To start the service:\n"
      "• Run this application as Administrator, or\n"
      "• Open Windows Services (services.msc) as Administrator\n"
      "• Find 'VEIL VPN Service' and click Start");
}

/// Service start failed
inline ErrorMessage service_start_failed(const std::string& error_details) {
  return ErrorMessage(
      ErrorCategory::kDaemon,
      "Service Start Failed",
      "The VEIL service failed to start.",
      "Possible solutions:\n"
      "• Check Windows Event Viewer for service errors\n"
      "• Ensure no other VPN software is conflicting\n"
      "• Reinstall VEIL VPN if the problem persists",
      error_details);
}

/// Generic IPC error
inline ErrorMessage ipc_error(const std::string& error_details) {
  return ErrorMessage(
      ErrorCategory::kDaemon,
      "Communication Error",
      "Failed to communicate with the VEIL daemon service.",
      "Try these steps:\n"
      "• Restart the VEIL service from Windows Services\n"
      "• Restart this application\n"
      "• If the problem persists, reinstall VEIL VPN",
      error_details);
}

/// Generic error with custom message
inline ErrorMessage generic(const std::string& message) {
  return ErrorMessage(
      ErrorCategory::kUnknown,
      "Error",
      message,
      "If this problem persists, please contact support.");
}

}  // namespace errors

}  // namespace veil::gui
