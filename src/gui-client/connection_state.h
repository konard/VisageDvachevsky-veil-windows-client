#pragma once

namespace veil::gui {

/// Connection states as defined in the UI design spec
enum class ConnectionState {
  kDisconnected,
  kConnecting,
  kConnected,
  kReconnecting,
  kError
};

}  // namespace veil::gui
