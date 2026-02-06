// Windows IPC implementation using Named Pipes
// This file is only compiled on Windows platforms

#ifdef _WIN32

#include "ipc_socket.h"

#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <system_error>

#include "common/logging/logger.h"

namespace veil::ipc {

namespace {
constexpr int kMaxPendingConnections = 5;
constexpr std::size_t kBufferSize = 4096;
constexpr DWORD kPipeBufferSize = 65536;
constexpr DWORD kDefaultTimeout = 5000;  // 5 seconds

// Convert Unix-style socket path to Windows named pipe path
std::string to_pipe_path(const std::string& socket_path) {
  // Convert /tmp/veil-client.sock to \\.\pipe\veil-client
  std::string name = socket_path;

  // Extract just the filename
  auto pos = name.rfind('/');
  if (pos != std::string::npos) {
    name = name.substr(pos + 1);
  }
  pos = name.rfind('\\');
  if (pos != std::string::npos) {
    name = name.substr(pos + 1);
  }

  // Remove .sock extension
  pos = name.rfind(".sock");
  if (pos != std::string::npos) {
    name = name.substr(0, pos);
  }

  return "\\\\.\\pipe\\" + name;
}

std::error_code last_error() {
  return std::error_code(static_cast<int>(GetLastError()), std::system_category());
}

// Creates a security descriptor that allows only authenticated users to access the pipe
// This prevents unauthenticated or unprivileged processes from connecting
// Returns: pointer to security descriptor that must be freed with LocalFree()
PSECURITY_DESCRIPTOR create_pipe_security_descriptor() {
  PSECURITY_DESCRIPTOR pSD = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
          "D:(A;;GA;;;AU)",  // Allow all access to authenticated users only
          SDDL_REVISION_1, &pSD, nullptr)) {
    return nullptr;
  }
  return pSD;
}

}  // namespace

// ============================================================================
// IPC Server Implementation (Named Pipes)
// ============================================================================

struct IpcServerImpl {
  HANDLE pipe{INVALID_HANDLE_VALUE};
  std::vector<HANDLE> clients;
  std::map<int, std::string> receive_buffers;  // fd -> receive buffer (persistent across poll() calls)
  std::vector<OVERLAPPED> overlapped_ops;

  // Persistent connection acceptance state
  OVERLAPPED accept_overlap{};
  bool accept_pending{false};
};

IpcServer::IpcServer(std::string socket_path)
    : socket_path_(to_pipe_path(socket_path)), impl_(std::make_unique<IpcServerImpl>()) {}

IpcServer::~IpcServer() {
  stop();
}

bool IpcServer::start(std::error_code& ec) {
  if (running_) {
    ec = std::make_error_code(std::errc::already_connected);
    return false;
  }

  // Create security descriptor for the pipe
  // Allow authenticated users to connect
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = FALSE;

  // Create a security descriptor that allows local authenticated users
  PSECURITY_DESCRIPTOR pSD = create_pipe_security_descriptor();
  if (!pSD) {
    ec = last_error();
    LOG_ERROR("Failed to create security descriptor: {}", ec.message());
    return false;
  }
  sa.lpSecurityDescriptor = pSD;

  // Create named pipe
  impl_->pipe = CreateNamedPipeA(
      socket_path_.c_str(),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
      kMaxPendingConnections,
      kPipeBufferSize,
      kPipeBufferSize,
      kDefaultTimeout,
      &sa);

  LocalFree(pSD);

  if (impl_->pipe == INVALID_HANDLE_VALUE) {
    ec = last_error();
    LOG_ERROR("Failed to create named pipe '{}': {}", socket_path_, ec.message());
    return false;
  }

  // Initialize persistent OVERLAPPED structure for accepting connections
  impl_->accept_overlap = {};
  impl_->accept_overlap.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
  if (impl_->accept_overlap.hEvent == nullptr) {
    ec = last_error();
    LOG_ERROR("Failed to create accept event: {}", ec.message());
    CloseHandle(impl_->pipe);
    impl_->pipe = INVALID_HANDLE_VALUE;
    return false;
  }

  running_ = true;
  LOG_INFO("IPC server started on {}", socket_path_);
  return true;
}

void IpcServer::stop() {
  if (!running_) {
    return;
  }

  running_ = false;

  // Cancel any pending accept operation
  if (impl_->accept_pending && impl_->pipe != INVALID_HANDLE_VALUE) {
    CancelIo(impl_->pipe);
    impl_->accept_pending = false;
  }

  // Close accept event handle
  if (impl_->accept_overlap.hEvent != nullptr) {
    CloseHandle(impl_->accept_overlap.hEvent);
    impl_->accept_overlap.hEvent = nullptr;
  }

  // Close all client connections
  for (auto& client : impl_->clients) {
    if (client != INVALID_HANDLE_VALUE) {
      DisconnectNamedPipe(client);
      CloseHandle(client);
    }
  }
  impl_->clients.clear();
  impl_->receive_buffers.clear();

  // Close server pipe
  if (impl_->pipe != INVALID_HANDLE_VALUE) {
    CloseHandle(impl_->pipe);
    impl_->pipe = INVALID_HANDLE_VALUE;
  }

  LOG_INFO("IPC server stopped");
}

void IpcServer::on_message(MessageHandler handler) {
  message_handler_ = std::move(handler);
}

bool IpcServer::send_message(int client_fd, const Message& msg, std::error_code& ec) {
  if (client_fd < 0 || static_cast<std::size_t>(client_fd) >= impl_->clients.size()) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }

  HANDLE client = impl_->clients[static_cast<std::size_t>(client_fd)];
  if (client == INVALID_HANDLE_VALUE) {
    ec = std::make_error_code(std::errc::not_connected);
    return false;
  }

  std::string data = serialize_message(msg);
  return send_raw(client, data, ec);
}

void IpcServer::broadcast_message(const Message& msg) {
  std::string data = serialize_message(msg);

  for (std::size_t i = 0; i < impl_->clients.size(); ++i) {
    if (impl_->clients[i] != INVALID_HANDLE_VALUE) {
      std::error_code ec;
      send_raw(impl_->clients[i], data, ec);
      // Ignore errors for broadcast
    }
  }
}

void IpcServer::poll(std::error_code& ec) {
  if (!running_) {
    ec = std::make_error_code(std::errc::not_connected);
    return;
  }

  // Accept new connections
  accept_connection(ec);

  // Handle data from existing clients
  for (std::size_t i = 0; i < impl_->clients.size(); ++i) {
    if (impl_->clients[i] != INVALID_HANDLE_VALUE) {
      ClientConnection conn;
      conn.fd = static_cast<int>(i);
      // Restore persistent receive buffer for this client
      conn.receive_buffer = impl_->receive_buffers[conn.fd];
      handle_client_data(conn, ec);
      // Save updated receive buffer back
      impl_->receive_buffers[conn.fd] = conn.receive_buffer;
    }
  }

  // Remove disconnected clients
  for (std::size_t i = 0; i < impl_->clients.size(); ++i) {
    if (impl_->clients[i] == INVALID_HANDLE_VALUE) {
      impl_->clients.erase(impl_->clients.begin() + static_cast<std::ptrdiff_t>(i));
      --i;
    }
  }
}

void IpcServer::accept_connection(std::error_code& ec) {
  // If we have a pending connection, check if it completed
  if (impl_->accept_pending) {
    DWORD wait_result = WaitForSingleObject(impl_->accept_overlap.hEvent, 0);
    if (wait_result == WAIT_OBJECT_0) {
      // Connection completed - move pipe to clients and create new one
      impl_->clients.push_back(impl_->pipe);
      impl_->accept_pending = false;

      LOG_DEBUG("Client connected to IPC server");

      // Create a new pipe instance for the next client with proper security
      SECURITY_ATTRIBUTES sa;
      sa.nLength = sizeof(SECURITY_ATTRIBUTES);
      sa.bInheritHandle = FALSE;
      PSECURITY_DESCRIPTOR pSD = create_pipe_security_descriptor();
      sa.lpSecurityDescriptor = pSD;

      impl_->pipe = CreateNamedPipeA(
          socket_path_.c_str(),
          PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
          PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
          kMaxPendingConnections,
          kPipeBufferSize,
          kPipeBufferSize,
          kDefaultTimeout,
          &sa);

      // Free the security descriptor after pipe creation
      if (pSD) {
        LocalFree(pSD);
      }

      if (impl_->pipe == INVALID_HANDLE_VALUE) {
        ec = last_error();
        LOG_ERROR("Failed to create new pipe instance: {}", ec.message());
        return;
      }

      // Reset the event for the next accept operation
      ResetEvent(impl_->accept_overlap.hEvent);
    }
    // Still pending, nothing to do
    return;
  }

  // No pending connection, initiate a new accept operation
  BOOL connected = ConnectNamedPipe(impl_->pipe, &impl_->accept_overlap);
  DWORD error = GetLastError();

  if (connected || error == ERROR_PIPE_CONNECTED) {
    // Client connected immediately (synchronous)
    impl_->clients.push_back(impl_->pipe);

    LOG_DEBUG("Client connected to IPC server");

    // Create a new pipe instance for the next client with proper security
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = FALSE;
    PSECURITY_DESCRIPTOR pSD = create_pipe_security_descriptor();
    sa.lpSecurityDescriptor = pSD;

    impl_->pipe = CreateNamedPipeA(
        socket_path_.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        kMaxPendingConnections,
        kPipeBufferSize,
        kPipeBufferSize,
        kDefaultTimeout,
        &sa);

    // Free the security descriptor after pipe creation
    if (pSD) {
      LocalFree(pSD);
    }

    if (impl_->pipe == INVALID_HANDLE_VALUE) {
      ec = last_error();
      LOG_ERROR("Failed to create new pipe instance: {}", ec.message());
      return;
    }

    // Reset the event for the next accept operation
    ResetEvent(impl_->accept_overlap.hEvent);
  } else if (error == ERROR_IO_PENDING) {
    // Connection is now pending asynchronously
    impl_->accept_pending = true;
  } else {
    // Some other error occurred
    ec = std::error_code(static_cast<int>(error), std::system_category());
    LOG_ERROR("ConnectNamedPipe failed: {}", ec.message());
  }
}

void IpcServer::handle_client_data(ClientConnection& conn, std::error_code& ec) {
  if (static_cast<std::size_t>(conn.fd) >= impl_->clients.size()) {
    return;
  }

  HANDLE client = impl_->clients[static_cast<std::size_t>(conn.fd)];
  if (client == INVALID_HANDLE_VALUE) {
    return;
  }

  char buffer[kBufferSize];
  DWORD bytes_read = 0;
  DWORD available = 0;

  // Check if data is available
  if (!PeekNamedPipe(client, nullptr, 0, nullptr, &available, nullptr)) {
    DWORD error = GetLastError();
    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
      remove_client(conn.fd);
      return;
    }
    return;
  }

  if (available == 0) {
    return;  // No data available
  }

  #ifdef VEIL_IPC_DEBUG
  std::cerr << "[IPC Server Debug] Data available on client " << conn.fd
            << ": " << available << " bytes" << std::endl;
  #endif

  // Read data
  if (!ReadFile(client, buffer, sizeof(buffer) - 1, &bytes_read, nullptr)) {
    DWORD error = GetLastError();
    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
      remove_client(conn.fd);
      return;
    }
    ec = std::error_code(static_cast<int>(error), std::system_category());
    return;
  }

  if (bytes_read == 0) {
    // Client disconnected
    remove_client(conn.fd);
    return;
  }

  #ifdef VEIL_IPC_DEBUG
  std::cerr << "[IPC Server Debug] Read " << bytes_read << " bytes from client " << conn.fd << std::endl;
  #endif

  // Null-terminate and process
  buffer[bytes_read] = '\0';

  #ifdef VEIL_IPC_DEBUG
  std::cerr << "[IPC Server Debug] Buffer content: " << buffer << std::endl;
  #endif

  conn.receive_buffer += buffer;

  #ifdef VEIL_IPC_DEBUG
  std::cerr << "[IPC Server Debug] receive_buffer size: " << conn.receive_buffer.size()
            << ", contains newline: " << (conn.receive_buffer.find('\n') != std::string::npos ? "YES" : "NO") << std::endl;
  #endif

  // Process complete messages (newline-delimited)
  std::size_t pos = 0;
  while ((pos = conn.receive_buffer.find('\n')) != std::string::npos) {
    std::string message_str = conn.receive_buffer.substr(0, pos);
    conn.receive_buffer.erase(0, pos + 1);

    #ifdef VEIL_IPC_DEBUG
    std::cerr << "[IPC Server Debug] Received message from client " << conn.fd
              << ": " << message_str << std::endl;
    #endif

    // Deserialize and handle message
    auto msg = deserialize_message(message_str);

    #ifdef VEIL_IPC_DEBUG
    std::cerr << "[IPC Server Debug] Deserialization result: " << (msg.has_value() ? "SUCCESS" : "FAILED") << std::endl;
    std::cerr << "[IPC Server Debug] message_handler_ is set: " << (message_handler_ ? "YES" : "NO") << std::endl;
    #endif

    if (msg && message_handler_) {
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Server Debug] Calling message_handler_" << std::endl;
      #endif
      message_handler_(*msg, conn.fd);
      #ifdef VEIL_IPC_DEBUG
      std::cerr << "[IPC Server Debug] message_handler_ returned" << std::endl;
      #endif
    } else {
      #ifdef VEIL_IPC_DEBUG
      if (!msg) std::cerr << "[IPC Server Debug] Message deserialization FAILED!" << std::endl;
      if (!message_handler_) std::cerr << "[IPC Server Debug] message_handler_ is NOT SET!" << std::endl;
      #endif
    }
  }
}

void IpcServer::remove_client(int client_fd) {
  if (client_fd < 0 || static_cast<std::size_t>(client_fd) >= impl_->clients.size()) {
    return;
  }

  HANDLE client = impl_->clients[static_cast<std::size_t>(client_fd)];
  if (client != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(client);
    CloseHandle(client);
    impl_->clients[static_cast<std::size_t>(client_fd)] = INVALID_HANDLE_VALUE;
  }

  // Clean up receive buffer for this client
  impl_->receive_buffers.erase(client_fd);

  LOG_DEBUG("Client {} disconnected", client_fd);
}

bool IpcServer::send_raw(HANDLE client, const std::string& data, std::error_code& ec) {
  DWORD bytes_written = 0;
  if (!WriteFile(client, data.c_str(), static_cast<DWORD>(data.size()),
                 &bytes_written, nullptr)) {
    ec = last_error();
    return false;
  }
  return bytes_written == data.size();
}

// ============================================================================
// IPC Client Implementation (Named Pipes)
// ============================================================================

struct IpcClientImpl {
  HANDLE pipe{INVALID_HANDLE_VALUE};
};

IpcClient::IpcClient(std::string socket_path)
    : socket_path_(to_pipe_path(socket_path)), impl_(std::make_unique<IpcClientImpl>()) {}

IpcClient::~IpcClient() {
  disconnect();
}

bool IpcClient::connect(std::error_code& ec) {
  if (connected_) {
    ec = std::make_error_code(std::errc::already_connected);
    return false;
  }

  // Non-blocking single attempt to connect to the named pipe.
  // Retries are handled at a higher level (IpcClientManager reconnect timer)
  // to avoid blocking the UI thread with Sleep() calls.
  impl_->pipe = CreateFileA(
      socket_path_.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      FILE_FLAG_OVERLAPPED,
      nullptr);

  if (impl_->pipe == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();

    // If the pipe is busy, wait briefly for it (non-blocking with short timeout)
    if (error == ERROR_PIPE_BUSY) {
      if (!WaitNamedPipeA(socket_path_.c_str(), 1000)) {
        ec = last_error();
        LOG_ERROR("Named pipe busy and wait failed: {}", ec.message());
        return false;
      }

      // Try again after pipe became available
      impl_->pipe = CreateFileA(
          socket_path_.c_str(),
          GENERIC_READ | GENERIC_WRITE,
          0,
          nullptr,
          OPEN_EXISTING,
          FILE_FLAG_OVERLAPPED,
          nullptr);

      if (impl_->pipe == INVALID_HANDLE_VALUE) {
        ec = last_error();
        LOG_ERROR("Failed to connect to named pipe '{}' after busy wait: {}",
                  socket_path_, ec.message());
        return false;
      }
    } else {
      // Failed - log and return error
      ec = last_error();
      if (error == ERROR_FILE_NOT_FOUND) {
        LOG_DEBUG("Daemon not running, pipe '{}' does not exist", socket_path_);
      } else {
        LOG_ERROR("Failed to connect to named pipe '{}': {}", socket_path_, ec.message());
      }
      return false;
    }
  }

  // Set pipe to message mode
  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(impl_->pipe, &mode, nullptr, nullptr)) {
    ec = last_error();
    LOG_ERROR("Failed to set pipe mode: {}", ec.message());
    CloseHandle(impl_->pipe);
    impl_->pipe = INVALID_HANDLE_VALUE;
    return false;
  }

  connected_ = true;
  socket_fd_ = 1;  // Dummy value for compatibility

  if (connection_handler_) {
    connection_handler_(true);
  }

  LOG_INFO("Connected to IPC server at {}", socket_path_);
  return true;
}

void IpcClient::disconnect() {
  if (!connected_) {
    return;
  }

  connected_ = false;
  socket_fd_ = -1;

  if (impl_->pipe != INVALID_HANDLE_VALUE) {
    CloseHandle(impl_->pipe);
    impl_->pipe = INVALID_HANDLE_VALUE;
  }

  if (connection_handler_) {
    connection_handler_(false);
  }

  LOG_INFO("Disconnected from IPC server");
}

bool IpcClient::send_command(const Command& cmd, std::error_code& ec) {
  Message msg;
  msg.type = MessageType::kCommand;
  msg.payload = cmd;
  return send_message(msg, ec);
}

bool IpcClient::send_command(const Command& cmd, std::uint64_t request_id, std::error_code& ec) {
  Message msg;
  msg.type = MessageType::kCommand;
  msg.id = request_id;
  msg.payload = cmd;
  return send_message(msg, ec);
}

void IpcClient::on_message(MessageHandler handler) {
  message_handler_ = std::move(handler);
}

void IpcClient::on_connection_change(ConnectionHandler handler) {
  connection_handler_ = std::move(handler);
}

void IpcClient::on_deserialization_error(DeserializationErrorHandler handler) {
  deserialization_error_handler_ = std::move(handler);
}

void IpcClient::poll(std::error_code& ec) {
  if (!connected_) {
    ec = std::make_error_code(std::errc::not_connected);
    return;
  }

  handle_incoming_data(ec);
}

bool IpcClient::send_message(const Message& msg, std::error_code& ec) {
  if (!connected_) {
    ec = std::make_error_code(std::errc::not_connected);
    return false;
  }

  std::string data = serialize_message(msg);
  return send_raw(data, ec);
}

bool IpcClient::send_raw(const std::string& data, std::error_code& ec) {
  DWORD bytes_written = 0;
  if (!WriteFile(impl_->pipe, data.c_str(), static_cast<DWORD>(data.size()),
                 &bytes_written, nullptr)) {
    DWORD error = GetLastError();
    ec = std::error_code(static_cast<int>(error), std::system_category());
    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
      disconnect();
    }
    return false;
  }
  return bytes_written == data.size();
}

void IpcClient::handle_incoming_data(std::error_code& ec) {
  DWORD available = 0;

  // Check if data is available
  if (!PeekNamedPipe(impl_->pipe, nullptr, 0, nullptr, &available, nullptr)) {
    DWORD error = GetLastError();
    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
      disconnect();
      return;
    }
    return;
  }

  if (available == 0) {
    return;  // No data available
  }

  char buffer[kBufferSize];
  DWORD bytes_read = 0;

  if (!ReadFile(impl_->pipe, buffer, sizeof(buffer) - 1, &bytes_read, nullptr)) {
    DWORD error = GetLastError();
    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
      disconnect();
      return;
    }
    ec = std::error_code(static_cast<int>(error), std::system_category());
    return;
  }

  if (bytes_read == 0) {
    // Server disconnected
    disconnect();
    return;
  }

  // Null-terminate and process
  buffer[bytes_read] = '\0';
  receive_buffer_ += buffer;

  // Process complete messages (newline-delimited)
  std::size_t pos = 0;
  while ((pos = receive_buffer_.find('\n')) != std::string::npos) {
    std::string message_str = receive_buffer_.substr(0, pos);
    receive_buffer_.erase(0, pos + 1);

    // Deserialize and handle message
    auto msg = deserialize_message(message_str);
    if (msg && message_handler_) {
      message_handler_(*msg);
    } else if (!msg && deserialization_error_handler_) {
      deserialization_error_handler_(message_str);
    }
  }
}

}  // namespace veil::ipc

#endif  // _WIN32
