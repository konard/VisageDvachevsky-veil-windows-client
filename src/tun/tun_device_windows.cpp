// Windows TUN device implementation using Wintun
// This file is only compiled on Windows platforms

#ifdef _WIN32

#include "tun/tun_device.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <rpc.h>

#include <array>
#include <atomic>
#include <thread>
#include <system_error>

#include "common/logging/logger.h"

// Wintun API types and function pointers
// These are loaded dynamically from wintun.dll

namespace {

// Wintun types
typedef void* WINTUN_ADAPTER_HANDLE;
typedef void* WINTUN_SESSION_HANDLE;
typedef struct _WINTUN_PACKET WINTUN_PACKET;

// Wintun function pointer types
typedef WINTUN_ADAPTER_HANDLE(WINAPI* WINTUN_CREATE_ADAPTER_FUNC)(
    LPCWSTR Name, LPCWSTR TunnelType, const GUID* RequestedGUID);
typedef WINTUN_ADAPTER_HANDLE(WINAPI* WINTUN_OPEN_ADAPTER_FUNC)(LPCWSTR Name);
typedef void(WINAPI* WINTUN_CLOSE_ADAPTER_FUNC)(WINTUN_ADAPTER_HANDLE Adapter);
typedef void(WINAPI* WINTUN_DELETE_DRIVER_FUNC)();
typedef void(WINAPI* WINTUN_GET_ADAPTER_LUID_FUNC)(
    WINTUN_ADAPTER_HANDLE Adapter, NET_LUID* Luid);
typedef WINTUN_SESSION_HANDLE(WINAPI* WINTUN_START_SESSION_FUNC)(
    WINTUN_ADAPTER_HANDLE Adapter, DWORD Capacity);
typedef void(WINAPI* WINTUN_END_SESSION_FUNC)(WINTUN_SESSION_HANDLE Session);
typedef HANDLE(WINAPI* WINTUN_GET_READ_WAIT_EVENT_FUNC)(WINTUN_SESSION_HANDLE Session);
typedef BYTE*(WINAPI* WINTUN_RECEIVE_PACKET_FUNC)(
    WINTUN_SESSION_HANDLE Session, DWORD* PacketSize);
typedef void(WINAPI* WINTUN_RELEASE_RECEIVE_PACKET_FUNC)(
    WINTUN_SESSION_HANDLE Session, const BYTE* Packet);
typedef BYTE*(WINAPI* WINTUN_ALLOCATE_SEND_PACKET_FUNC)(
    WINTUN_SESSION_HANDLE Session, DWORD PacketSize);
typedef void(WINAPI* WINTUN_SEND_PACKET_FUNC)(
    WINTUN_SESSION_HANDLE Session, const BYTE* Packet);
typedef DWORD(WINAPI* WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC)();
typedef void(WINAPI* WINTUN_SET_LOGGER_FUNC)(void* NewLogger);

// Wintun function pointers
struct WintunAPI {
  HMODULE module{nullptr};
  WINTUN_CREATE_ADAPTER_FUNC CreateAdapter{nullptr};
  WINTUN_OPEN_ADAPTER_FUNC OpenAdapter{nullptr};
  WINTUN_CLOSE_ADAPTER_FUNC CloseAdapter{nullptr};
  WINTUN_DELETE_DRIVER_FUNC DeleteDriver{nullptr};
  WINTUN_GET_ADAPTER_LUID_FUNC GetAdapterLUID{nullptr};
  WINTUN_START_SESSION_FUNC StartSession{nullptr};
  WINTUN_END_SESSION_FUNC EndSession{nullptr};
  WINTUN_GET_READ_WAIT_EVENT_FUNC GetReadWaitEvent{nullptr};
  WINTUN_RECEIVE_PACKET_FUNC ReceivePacket{nullptr};
  WINTUN_RELEASE_RECEIVE_PACKET_FUNC ReleaseReceivePacket{nullptr};
  WINTUN_ALLOCATE_SEND_PACKET_FUNC AllocateSendPacket{nullptr};
  WINTUN_SEND_PACKET_FUNC SendPacket{nullptr};
  WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC GetRunningDriverVersion{nullptr};
  WINTUN_SET_LOGGER_FUNC SetLogger{nullptr};

  bool loaded{false};
};

// Global Wintun API instance
WintunAPI g_wintun;

// Load Wintun DLL and resolve function pointers
bool load_wintun_api(std::error_code& ec) {
  if (g_wintun.loaded) {
    return true;
  }

  g_wintun.module = LoadLibraryExW(L"wintun.dll", nullptr,
                                    LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                    LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!g_wintun.module) {
    ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    LOG_ERROR("Failed to load wintun.dll: {}", ec.message());
    return false;
  }

#define LOAD_WINTUN_FUNC(field, funcname, typedef_name) \
  g_wintun.field = reinterpret_cast<typedef_name>( \
      GetProcAddress(g_wintun.module, funcname)); \
  if (!g_wintun.field) { \
    ec = std::error_code(static_cast<int>(GetLastError()), std::system_category()); \
    LOG_ERROR("Failed to load {}: {}", funcname, ec.message()); \
    FreeLibrary(g_wintun.module); \
    g_wintun.module = nullptr; \
    return false; \
  }

  LOAD_WINTUN_FUNC(CreateAdapter, "WintunCreateAdapter", WINTUN_CREATE_ADAPTER_FUNC);
  LOAD_WINTUN_FUNC(OpenAdapter, "WintunOpenAdapter", WINTUN_OPEN_ADAPTER_FUNC);
  LOAD_WINTUN_FUNC(CloseAdapter, "WintunCloseAdapter", WINTUN_CLOSE_ADAPTER_FUNC);
  LOAD_WINTUN_FUNC(GetAdapterLUID, "WintunGetAdapterLUID", WINTUN_GET_ADAPTER_LUID_FUNC);
  LOAD_WINTUN_FUNC(StartSession, "WintunStartSession", WINTUN_START_SESSION_FUNC);
  LOAD_WINTUN_FUNC(EndSession, "WintunEndSession", WINTUN_END_SESSION_FUNC);
  LOAD_WINTUN_FUNC(GetReadWaitEvent, "WintunGetReadWaitEvent", WINTUN_GET_READ_WAIT_EVENT_FUNC);
  LOAD_WINTUN_FUNC(ReceivePacket, "WintunReceivePacket", WINTUN_RECEIVE_PACKET_FUNC);
  LOAD_WINTUN_FUNC(ReleaseReceivePacket, "WintunReleaseReceivePacket", WINTUN_RELEASE_RECEIVE_PACKET_FUNC);
  LOAD_WINTUN_FUNC(AllocateSendPacket, "WintunAllocateSendPacket", WINTUN_ALLOCATE_SEND_PACKET_FUNC);
  LOAD_WINTUN_FUNC(SendPacket, "WintunSendPacket", WINTUN_SEND_PACKET_FUNC);
  LOAD_WINTUN_FUNC(GetRunningDriverVersion, "WintunGetRunningDriverVersion", WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC);

#undef LOAD_WINTUN_FUNC

  // Optional functions
  g_wintun.DeleteDriver = reinterpret_cast<WINTUN_DELETE_DRIVER_FUNC>(
      GetProcAddress(g_wintun.module, "WintunDeleteDriver"));
  g_wintun.SetLogger = reinterpret_cast<WINTUN_SET_LOGGER_FUNC>(
      GetProcAddress(g_wintun.module, "WintunSetLogger"));

  g_wintun.loaded = true;
  LOG_INFO("Wintun DLL loaded, driver version: {}", g_wintun.GetRunningDriverVersion());
  return true;
}

std::error_code last_error() {
  return std::error_code(static_cast<int>(GetLastError()), std::system_category());
}

// Convert narrow string to wide string
std::wstring to_wide(const std::string& str) {
  if (str.empty()) return L"";
  int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                                  static_cast<int>(str.size()), nullptr, 0);
  std::wstring result(size, 0);
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()),
                      &result[0], size);
  return result;
}

// Convert wide string to narrow string
std::string to_narrow(const std::wstring& wstr) {
  if (wstr.empty()) return "";
  int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
                                  static_cast<int>(wstr.size()), nullptr, 0,
                                  nullptr, nullptr);
  std::string result(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()),
                      &result[0], size, nullptr, nullptr);
  return result;
}

// Maximum packet size for TUN devices
constexpr std::size_t kMaxPacketSize = 65535;

// Default session capacity (ring buffer size)
constexpr DWORD kSessionCapacity = 0x400000;  // 4 MB

}  // namespace

namespace veil::tun {

// Windows-specific implementation details
struct TunDeviceImpl {
  WINTUN_ADAPTER_HANDLE adapter{nullptr};
  WINTUN_SESSION_HANDLE session{nullptr};
  HANDLE read_event{nullptr};
  NET_LUID luid{};
  std::atomic<bool> running{false};
};

TunDevice::TunDevice() = default;

TunDevice::~TunDevice() { close(); }

TunDevice::TunDevice(TunDevice&& other) noexcept
    : fd_(other.fd_),
      device_name_(std::move(other.device_name_)),
      stats_(other.stats_),
      packet_info_(other.packet_info_),
      impl_(std::move(other.impl_)) {
  other.fd_ = -1;
  other.impl_ = nullptr;
}

TunDevice& TunDevice::operator=(TunDevice&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = other.fd_;
    device_name_ = std::move(other.device_name_);
    stats_ = other.stats_;
    packet_info_ = other.packet_info_;
    impl_ = std::move(other.impl_);
    other.fd_ = -1;
    other.impl_ = nullptr;
  }
  return *this;
}

bool TunDevice::open(const TunConfig& config, std::error_code& ec) {
  // Load Wintun API if not already loaded
  if (!load_wintun_api(ec)) {
    return false;
  }

  // Create implementation struct
  impl_ = std::make_unique<TunDeviceImpl>();

  // Generate or use provided device name
  std::wstring device_name_wide;
  if (config.device_name.empty()) {
    device_name_wide = L"veil0";
    device_name_ = "veil0";
  } else {
    device_name_wide = to_wide(config.device_name);
    device_name_ = config.device_name;
  }

  // Generate a GUID for the adapter (deterministic based on name)
  GUID adapter_guid;
  if (UuidCreate(&adapter_guid) != RPC_S_OK) {
    ec = std::make_error_code(std::errc::operation_not_permitted);
    LOG_ERROR("Failed to create GUID for adapter");
    return false;
  }

  // Create the adapter
  impl_->adapter = g_wintun.CreateAdapter(
      device_name_wide.c_str(), L"VEIL VPN", &adapter_guid);
  if (!impl_->adapter) {
    ec = last_error();
    LOG_ERROR("Failed to create Wintun adapter '{}': {}", device_name_, ec.message());

    // Provide helpful error message for common issues
    if (ec.value() == ERROR_ACCESS_DENIED) {
      LOG_ERROR("========================================");
      LOG_ERROR("ACCESS DENIED ERROR");
      LOG_ERROR("========================================");
      LOG_ERROR("Creating virtual network adapters requires administrator privileges.");
      LOG_ERROR("Please ensure the service is running with administrator rights:");
      LOG_ERROR("  - If using --debug mode: Run from elevated PowerShell/Command Prompt");
      LOG_ERROR("  - If installed as service: The service should run as SYSTEM automatically");
      LOG_ERROR("========================================");
    }

    impl_.reset();
    return false;
  }

  LOG_INFO("Created Wintun adapter: {}", device_name_);

  // Get the adapter LUID for IP configuration
  // Note: WintunGetAdapterLUID returns void, not an error code
  g_wintun.GetAdapterLUID(impl_->adapter, &impl_->luid);

  // Configure IP address if provided
  if (!config.ip_address.empty()) {
    if (!configure_address(config, ec)) {
      g_wintun.CloseAdapter(impl_->adapter);
      impl_.reset();
      return false;
    }
  }

  // Set MTU
  if (config.mtu > 0) {
    if (!configure_mtu(config.mtu, ec)) {
      g_wintun.CloseAdapter(impl_->adapter);
      impl_.reset();
      return false;
    }
  }

  // Start the session
  impl_->session = g_wintun.StartSession(impl_->adapter, kSessionCapacity);
  if (!impl_->session) {
    ec = last_error();
    LOG_ERROR("Failed to start Wintun session: {}", ec.message());
    g_wintun.CloseAdapter(impl_->adapter);
    impl_.reset();
    return false;
  }

  // Get read wait event for polling
  impl_->read_event = g_wintun.GetReadWaitEvent(impl_->session);
  impl_->running = true;

  // Set fd_ to a positive value to indicate the device is open
  // (Windows doesn't use file descriptors, but we use this for compatibility)
  fd_ = 1;

  LOG_INFO("Wintun session started successfully");
  return true;
}

void TunDevice::close() {
  if (!impl_) {
    return;
  }

  impl_->running = false;

  if (impl_->session) {
    g_wintun.EndSession(impl_->session);
    impl_->session = nullptr;
    LOG_INFO("Ended Wintun session");
  }

  if (impl_->adapter) {
    g_wintun.CloseAdapter(impl_->adapter);
    impl_->adapter = nullptr;
    LOG_INFO("Closed Wintun adapter: {}", device_name_);
  }

  impl_.reset();
  fd_ = -1;
}

bool TunDevice::configure_address(const TunConfig& config, std::error_code& ec) {
  // Parse IP address
  IN_ADDR addr;
  if (InetPtonA(AF_INET, config.ip_address.c_str(), &addr) != 1) {
    ec = std::make_error_code(std::errc::invalid_argument);
    LOG_ERROR("Invalid IP address: {}", config.ip_address);
    return false;
  }

  // Parse netmask and calculate prefix length
  IN_ADDR mask;
  if (InetPtonA(AF_INET, config.netmask.c_str(), &mask) != 1) {
    ec = std::make_error_code(std::errc::invalid_argument);
    LOG_ERROR("Invalid netmask: {}", config.netmask);
    return false;
  }

  // Count bits in netmask to get prefix length
  ULONG mask_bits = ntohl(mask.S_un.S_addr);
  UCHAR prefix_length = 0;
  while (mask_bits & 0x80000000) {
    prefix_length++;
    mask_bits <<= 1;
  }

  // Add unicast IP address
  MIB_UNICASTIPADDRESS_ROW ip_row;
  InitializeUnicastIpAddressEntry(&ip_row);
  ip_row.InterfaceLuid = impl_->luid;
  ip_row.Address.Ipv4.sin_family = AF_INET;
  ip_row.Address.Ipv4.sin_addr = addr;
  ip_row.OnLinkPrefixLength = prefix_length;
  ip_row.DadState = IpDadStatePreferred;

  DWORD result = CreateUnicastIpAddressEntry(&ip_row);
  if (result != NO_ERROR && result != ERROR_OBJECT_ALREADY_EXISTS) {
    ec = std::error_code(static_cast<int>(result), std::system_category());
    LOG_ERROR("Failed to set IP address: {}", ec.message());
    return false;
  }

  LOG_INFO("Set IP address {}/{} on {}", config.ip_address, prefix_length, device_name_);
  return true;
}

bool TunDevice::configure_mtu(int mtu, std::error_code& ec) {
  MIB_IPINTERFACE_ROW iface_row;
  InitializeIpInterfaceEntry(&iface_row);
  iface_row.InterfaceLuid = impl_->luid;
  iface_row.Family = AF_INET;

  DWORD result = GetIpInterfaceEntry(&iface_row);
  if (result != NO_ERROR) {
    ec = std::error_code(static_cast<int>(result), std::system_category());
    LOG_ERROR("Failed to get interface entry: {}", ec.message());
    return false;
  }

  iface_row.NlMtu = static_cast<ULONG>(mtu);
  iface_row.SitePrefixLength = 0;

  result = SetIpInterfaceEntry(&iface_row);
  if (result != NO_ERROR) {
    ec = std::error_code(static_cast<int>(result), std::system_category());
    LOG_ERROR("Failed to set MTU to {}: {}", mtu, ec.message());
    return false;
  }

  LOG_INFO("Set MTU {} on {}", mtu, device_name_);
  return true;
}

bool TunDevice::bring_interface_up(std::error_code& ec) {
  // Wintun adapter is automatically up when created
  LOG_INFO("Interface {} is up", device_name_);
  return true;
}

bool TunDevice::set_mtu(int mtu, std::error_code& ec) {
  return configure_mtu(mtu, ec);
}

bool TunDevice::set_up(bool up, std::error_code& ec) {
  // Wintun doesn't have a direct way to disable the adapter
  // through the API. The adapter is always up while the session exists.
  if (!up) {
    LOG_WARN("Wintun adapter cannot be set down while session is active");
  }
  return true;
}

std::optional<std::vector<std::uint8_t>> TunDevice::read(std::error_code& ec) {
  if (!impl_ || !impl_->session) {
    ec = std::make_error_code(std::errc::not_connected);
    return std::nullopt;
  }

  DWORD packet_size = 0;
  BYTE* packet = g_wintun.ReceivePacket(impl_->session, &packet_size);

  if (!packet) {
    DWORD error = GetLastError();
    if (error == ERROR_NO_MORE_ITEMS) {
      // No packet available
      return std::nullopt;
    }
    ec = std::error_code(static_cast<int>(error), std::system_category());
    stats_.read_errors++;
    return std::nullopt;
  }

  std::vector<std::uint8_t> result(packet, packet + packet_size);
  g_wintun.ReleaseReceivePacket(impl_->session, packet);

  stats_.packets_read++;
  stats_.bytes_read += packet_size;

  return result;
}

std::ptrdiff_t TunDevice::read_into(std::span<std::uint8_t> buffer, std::error_code& ec) {
  if (!impl_ || !impl_->session) {
    ec = std::make_error_code(std::errc::not_connected);
    return -1;
  }

  DWORD packet_size = 0;
  BYTE* packet = g_wintun.ReceivePacket(impl_->session, &packet_size);

  if (!packet) {
    DWORD error = GetLastError();
    if (error == ERROR_NO_MORE_ITEMS) {
      return 0;  // No data available
    }
    ec = std::error_code(static_cast<int>(error), std::system_category());
    stats_.read_errors++;
    return -1;
  }

  std::size_t copy_size = std::min(static_cast<std::size_t>(packet_size), buffer.size());
  std::memcpy(buffer.data(), packet, copy_size);
  g_wintun.ReleaseReceivePacket(impl_->session, packet);

  stats_.packets_read++;
  stats_.bytes_read += copy_size;

  return static_cast<std::ptrdiff_t>(copy_size);
}

bool TunDevice::write(std::span<const std::uint8_t> packet, std::error_code& ec) {
  if (!impl_ || !impl_->session) {
    ec = std::make_error_code(std::errc::not_connected);
    return false;
  }

  BYTE* send_packet = g_wintun.AllocateSendPacket(
      impl_->session, static_cast<DWORD>(packet.size()));

  if (!send_packet) {
    ec = last_error();
    stats_.write_errors++;
    return false;
  }

  std::memcpy(send_packet, packet.data(), packet.size());
  g_wintun.SendPacket(impl_->session, send_packet);

  stats_.packets_written++;
  stats_.bytes_written += packet.size();
  return true;
}

bool TunDevice::poll(const ReadHandler& handler, int timeout_ms, std::error_code& ec) {
  if (!impl_ || !impl_->session || !impl_->read_event) {
    ec = std::make_error_code(std::errc::not_connected);
    return false;
  }

  // Wait for data to be available
  DWORD result = WaitForSingleObject(impl_->read_event,
                                      timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms));

  if (result == WAIT_FAILED) {
    ec = last_error();
    return false;
  }

  if (result == WAIT_TIMEOUT) {
    return true;  // Timeout, no data
  }

  // Read all available packets
  while (impl_->running) {
    auto packet = read(ec);
    if (!packet) {
      break;  // No more packets or error
    }
    if (!packet->empty()) {
      handler(*packet);
    }
  }

  return true;
}

}  // namespace veil::tun

#endif  // _WIN32
