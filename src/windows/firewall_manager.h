#pragma once

#ifdef _WIN32

#include <windows.h>

#include <cstdint>
#include <string>

namespace veil::windows {

// ============================================================================
// Windows Firewall Manager
// ============================================================================
// Provides functionality to add and remove Windows Firewall rules using the
// Windows Firewall COM API (INetFwPolicy2). This is a more secure and robust
// alternative to using system() calls with netsh commands.
//
// Benefits over system() calls:
// - No risk of command injection (CWE-78)
// - Proper error handling via HRESULT codes
// - No visible cmd.exe window
// - Better performance (no process creation overhead)
//
// Thread Safety:
// - COM initialization must be done per-thread (handled internally)
// - Multiple FirewallManager instances can coexist safely
//
// Requirements:
// - Administrator privileges (same as netsh)
// - Windows Vista or later (for INetFwPolicy2)

class FirewallManager {
 public:
  // Rule direction
  enum class Direction {
    kInbound,
    kOutbound
  };

  // Network protocol
  enum class Protocol {
    kTCP = 6,
    kUDP = 17,
    kAny = 256
  };

  // Firewall action
  enum class Action {
    kAllow,
    kBlock
  };

  // Constructor/Destructor
  FirewallManager();
  ~FirewallManager();

  // Delete copy constructor and assignment operator (COM objects are not copyable)
  FirewallManager(const FirewallManager&) = delete;
  FirewallManager& operator=(const FirewallManager&) = delete;

  // Initialize the firewall manager (must be called before other methods)
  // Returns true on success, false on failure
  // error: Populated with error message on failure
  bool initialize(std::string& error);

  // Add a firewall rule
  // name: Unique name for the rule (used for later removal)
  // description: Human-readable description
  // direction: Inbound or outbound
  // protocol: TCP, UDP, or Any
  // local_port: Port number (0 = any port)
  // action: Allow or block
  // enabled: Whether the rule is enabled
  // error: Populated with error message on failure
  // Returns true on success, false on failure
  bool add_rule(const std::string& name,
                const std::string& description,
                Direction direction,
                Protocol protocol,
                std::uint16_t local_port,
                Action action,
                bool enabled,
                std::string& error);

  // Remove a firewall rule by name
  // name: Name of the rule to remove
  // error: Populated with error message on failure
  // Returns true on success, false on failure (including if rule doesn't exist)
  bool remove_rule(const std::string& name, std::string& error);

  // Check if a rule exists
  // name: Name of the rule to check
  // Returns true if the rule exists, false otherwise
  bool rule_exists(const std::string& name);

 private:
  class Impl;
  Impl* impl_;
};

}  // namespace veil::windows

#endif  // _WIN32
