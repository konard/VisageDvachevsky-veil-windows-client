#ifdef _WIN32

#include "firewall_manager.h"

#include <comutil.h>
#include <netfw.h>

#include <memory>

#include "../common/logging/logger.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace veil::windows {

// ============================================================================
// FirewallManager Implementation (Pimpl idiom to hide COM details)
// ============================================================================

class FirewallManager::Impl {
 public:
  Impl() : policy_(nullptr), rules_(nullptr), com_initialized_(false) {}

  ~Impl() {
    cleanup();
  }

  bool initialize(std::string& error) {
    // Initialize COM for this thread
    HRESULT hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
      error = "Failed to initialize COM: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      return false;
    }
    com_initialized_ = (hr != RPC_E_CHANGED_MODE);

    // Create INetFwPolicy2 instance
    hr = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
                          __uuidof(INetFwPolicy2),
                          reinterpret_cast<void**>(&policy_));
    if (FAILED(hr)) {
      error = "Failed to create INetFwPolicy2 instance: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      cleanup();
      return false;
    }

    // Get the firewall rules collection
    hr = policy_->get_Rules(&rules_);
    if (FAILED(hr)) {
      error = "Failed to get firewall rules collection: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      cleanup();
      return false;
    }

    LOG_DEBUG("FirewallManager initialized successfully");
    return true;
  }

  bool add_rule(const std::string& name, const std::string& description,
                Direction direction, Protocol protocol, std::uint16_t local_port,
                Action action, bool enabled, std::string& error) {
    if (!rules_) {
      error = "FirewallManager not initialized";
      LOG_ERROR("{}", error);
      return false;
    }

    // Create a new firewall rule
    INetFwRule* rule = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(NetFwRule), nullptr,
                                  CLSCTX_INPROC_SERVER, __uuidof(INetFwRule),
                                  reinterpret_cast<void**>(&rule));
    if (FAILED(hr)) {
      error = "Failed to create INetFwRule instance: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      return false;
    }

    // Use RAII to ensure rule is released
    std::unique_ptr<INetFwRule, decltype(&release_com_ptr<INetFwRule>)> rule_ptr(
        rule, release_com_ptr<INetFwRule>);

    // Set rule name
    BSTR bstr_name = SysAllocString(to_wstring(name).c_str());
    if (!bstr_name) {
      error = "Failed to allocate BSTR for rule name";
      LOG_ERROR("{}", error);
      return false;
    }
    hr = rule->put_Name(bstr_name);
    SysFreeString(bstr_name);
    if (FAILED(hr)) {
      error = "Failed to set rule name: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      return false;
    }

    // Set rule description
    BSTR bstr_desc = SysAllocString(to_wstring(description).c_str());
    if (!bstr_desc) {
      error = "Failed to allocate BSTR for rule description";
      LOG_ERROR("{}", error);
      return false;
    }
    hr = rule->put_Description(bstr_desc);
    SysFreeString(bstr_desc);
    if (FAILED(hr)) {
      error = "Failed to set rule description: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      return false;
    }

    // Set protocol
    hr = rule->put_Protocol(static_cast<NET_FW_IP_PROTOCOL>(protocol));
    if (FAILED(hr)) {
      error = "Failed to set rule protocol: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      return false;
    }

    // Set local port (if specified)
    if (local_port > 0) {
      std::wstring port_str = std::to_wstring(local_port);
      BSTR bstr_port = SysAllocString(port_str.c_str());
      if (!bstr_port) {
        error = "Failed to allocate BSTR for port";
        LOG_ERROR("{}", error);
        return false;
      }
      hr = rule->put_LocalPorts(bstr_port);
      SysFreeString(bstr_port);
      if (FAILED(hr)) {
        error = "Failed to set rule local port: HRESULT 0x" + format_hresult(hr);
        LOG_ERROR("{}", error);
        return false;
      }
    }

    // Set direction
    NET_FW_RULE_DIRECTION dir =
        (direction == Direction::kInbound) ? NET_FW_RULE_DIR_IN : NET_FW_RULE_DIR_OUT;
    hr = rule->put_Direction(dir);
    if (FAILED(hr)) {
      error = "Failed to set rule direction: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      return false;
    }

    // Set action
    NET_FW_ACTION fw_action =
        (action == Action::kAllow) ? NET_FW_ACTION_ALLOW : NET_FW_ACTION_BLOCK;
    hr = rule->put_Action(fw_action);
    if (FAILED(hr)) {
      error = "Failed to set rule action: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      return false;
    }

    // Set enabled state
    hr = rule->put_Enabled(enabled ? VARIANT_TRUE : VARIANT_FALSE);
    if (FAILED(hr)) {
      error = "Failed to set rule enabled state: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      return false;
    }

    // Set profiles (apply to all profiles: Domain, Private, Public)
    hr = rule->put_Profiles(NET_FW_PROFILE2_ALL);
    if (FAILED(hr)) {
      error = "Failed to set rule profiles: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      return false;
    }

    // Add the rule to the firewall
    hr = rules_->Add(rule);
    if (FAILED(hr)) {
      error = "Failed to add rule to firewall: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      return false;
    }

    LOG_INFO("Firewall rule '{}' added successfully", name);
    return true;
  }

  bool remove_rule(const std::string& name, std::string& error) {
    if (!rules_) {
      error = "FirewallManager not initialized";
      LOG_ERROR("{}", error);
      return false;
    }

    BSTR bstr_name = SysAllocString(to_wstring(name).c_str());
    if (!bstr_name) {
      error = "Failed to allocate BSTR for rule name";
      LOG_ERROR("{}", error);
      return false;
    }

    HRESULT hr = rules_->Remove(bstr_name);
    SysFreeString(bstr_name);

    if (FAILED(hr)) {
      error = "Failed to remove rule: HRESULT 0x" + format_hresult(hr);
      LOG_ERROR("{}", error);
      return false;
    }

    LOG_INFO("Firewall rule '{}' removed successfully", name);
    return true;
  }

  bool rule_exists(const std::string& name) {
    if (!rules_) {
      return false;
    }

    BSTR bstr_name = SysAllocString(to_wstring(name).c_str());
    if (!bstr_name) {
      return false;
    }

    INetFwRule* rule = nullptr;
    HRESULT hr = rules_->Item(bstr_name, &rule);
    SysFreeString(bstr_name);

    if (SUCCEEDED(hr) && rule) {
      rule->Release();
      return true;
    }

    return false;
  }

 private:
  void cleanup() {
    if (rules_) {
      rules_->Release();
      rules_ = nullptr;
    }
    if (policy_) {
      policy_->Release();
      policy_ = nullptr;
    }
    if (com_initialized_) {
      CoUninitialize();
      com_initialized_ = false;
    }
  }

  static std::wstring to_wstring(const std::string& str) {
    if (str.empty()) {
      return std::wstring();
    }
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                                          static_cast<int>(str.size()), nullptr, 0);
    std::wstring result(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()),
                        &result[0], size_needed);
    return result;
  }

  static std::string format_hresult(HRESULT hr) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%08lX", static_cast<unsigned long>(hr));
    return std::string(buf);
  }

  template <typename T>
  static void release_com_ptr(T* ptr) {
    if (ptr) {
      ptr->Release();
    }
  }

  INetFwPolicy2* policy_;
  INetFwRules* rules_;
  bool com_initialized_;
};

// ============================================================================
// FirewallManager Public Interface
// ============================================================================

FirewallManager::FirewallManager() : impl_(new Impl()) {}

FirewallManager::~FirewallManager() {
  delete impl_;
}

bool FirewallManager::initialize(std::string& error) {
  return impl_->initialize(error);
}

bool FirewallManager::add_rule(const std::string& name,
                                const std::string& description,
                                Direction direction, Protocol protocol,
                                std::uint16_t local_port, Action action,
                                bool enabled, std::string& error) {
  return impl_->add_rule(name, description, direction, protocol, local_port,
                         action, enabled, error);
}

bool FirewallManager::remove_rule(const std::string& name, std::string& error) {
  return impl_->remove_rule(name, error);
}

bool FirewallManager::rule_exists(const std::string& name) {
  return impl_->rule_exists(name);
}

}  // namespace veil::windows

#endif  // _WIN32
