# Platform-specific source files
if(WIN32)
  set(VEIL_TUN_SOURCES tun/tun_device_windows.cpp)
  set(VEIL_ROUTING_SOURCES tun/routing_windows.cpp)
  set(VEIL_IPC_SOURCES common/ipc/ipc_socket_windows.cpp)
  set(VEIL_WINDOWS_SOURCES
    windows/service_manager.cpp
    windows/system_tray.cpp
    windows/console_handler.cpp
  )
  # Windows doesn't use POSIX signals or daemonization
  set(VEIL_SIGNAL_SOURCES)
  set(VEIL_DAEMON_SOURCES)
  # Transport layer now available on Windows with select-based event loop
  set(VEIL_TRANSPORT_SOURCES
    transport/udp_socket/udp_socket_windows.cpp
    transport/mux/ack_bitmap.cpp
    transport/mux/reorder_buffer.cpp
    transport/mux/fragment_reassembly.cpp
    transport/mux/mux_codec.cpp
    transport/mux/retransmit_buffer.cpp
    transport/mux/ack_scheduler.cpp
    transport/session/transport_session.cpp
    transport/event_loop/event_loop_windows.cpp
    transport/stats/transport_stats.cpp
  )
  set(VEIL_TUNNEL_SOURCES
    tunnel/tunnel.cpp
    tunnel/session_migration.cpp
  )
  set(VEIL_SERVER_SOURCES)
  set(VEIL_CLI_CONFIG_SOURCES)
else()
  set(VEIL_TUN_SOURCES tun/tun_device_linux.cpp)
  set(VEIL_ROUTING_SOURCES tun/routing_linux.cpp)
  set(VEIL_IPC_SOURCES common/ipc/ipc_socket_unix.cpp)
  set(VEIL_WINDOWS_SOURCES)
  # POSIX signals, daemonization, and epoll-based transport are available on Unix-like systems
  set(VEIL_SIGNAL_SOURCES common/signal/signal_handler.cpp)
  set(VEIL_DAEMON_SOURCES common/daemon/daemon.cpp)
  set(VEIL_TRANSPORT_SOURCES
    transport/udp_socket/udp_socket_linux.cpp
    transport/mux/ack_bitmap.cpp
    transport/mux/reorder_buffer.cpp
    transport/mux/fragment_reassembly.cpp
    transport/mux/mux_codec.cpp
    transport/mux/retransmit_buffer.cpp
    transport/mux/ack_scheduler.cpp
    transport/session/transport_session.cpp
    transport/event_loop/event_loop_linux.cpp
    transport/stats/transport_stats.cpp
  )
  set(VEIL_TUNNEL_SOURCES
    tunnel/tunnel.cpp
    tunnel/session_migration.cpp
  )
  set(VEIL_SERVER_SOURCES
    server/session_table.cpp
  )
  set(VEIL_CLI_CONFIG_SOURCES
    common/config/app_config.cpp
  )
endif()

add_library(veil_common STATIC
  common/crypto/random.cpp
  common/crypto/crypto_engine.cpp
  common/logging/logger.cpp
  common/logging/constrained_logger.cpp
  ${VEIL_CLI_CONFIG_SOURCES}
  common/packet/packet_builder.cpp
  common/session/replay_window.cpp
  common/session/session_rotator.cpp
  common/session/session_lifecycle.cpp
  common/session/idle_timeout.cpp
  common/handshake/handshake_processor.cpp
  common/handshake/handshake_replay_cache.cpp
  common/utils/rate_limiter.cpp
  common/utils/timer_heap.cpp
  common/utils/advanced_rate_limiter.cpp
  common/utils/graceful_degradation.cpp
  common/metrics/metrics.cpp
  common/obfuscation/obfuscation_profile.cpp
  common/protocol_wrapper/websocket_wrapper.cpp
  ${VEIL_SIGNAL_SOURCES}
  ${VEIL_DAEMON_SOURCES}
  common/ipc/ipc_protocol.cpp
  ${VEIL_IPC_SOURCES}
  ${VEIL_TRANSPORT_SOURCES}
  ${VEIL_TUN_SOURCES}
  ${VEIL_ROUTING_SOURCES}
  tun/mtu_discovery.cpp
  ${VEIL_TUNNEL_SOURCES}
  ${VEIL_SERVER_SOURCES}
  ${VEIL_WINDOWS_SOURCES}
)

target_include_directories(veil_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(veil_common
  PUBLIC
    Sodium::Sodium
    spdlog::spdlog
    fmt::fmt
    CLI11::CLI11
    nlohmann_json::nlohmann_json
)

# Windows-specific libraries
if(WIN32)
  target_link_libraries(veil_common
    PUBLIC
      iphlpapi
      ws2_32
      rpcrt4
  )
  target_compile_definitions(veil_common PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)

  # Enable IPC debug logging on Windows for easier troubleshooting
  target_compile_definitions(veil_common PUBLIC VEIL_IPC_DEBUG)
  message(STATUS "IPC debug logging enabled for Windows builds")
endif()

veil_set_warnings(veil_common)

if(TARGET sodium_ep)
  add_dependencies(veil_common sodium_ep)
endif()

# Client executable (requires transport layer).
# On Linux: uses epoll-based event loop
# On Windows: uses select-based event loop
if(NOT WIN32)
  add_executable(veil-client
    client/main.cpp
    client/client_config.cpp
  )

  target_link_libraries(veil-client PRIVATE veil_common)
  veil_set_warnings(veil-client)

  install(TARGETS veil-client
    RUNTIME DESTINATION bin
  )
endif()

# Server executable (optional, requires transport layer, not available on Windows yet).
option(VEIL_BUILD_SERVER "Build server executable" ON)

if(VEIL_BUILD_SERVER AND NOT WIN32)
  add_executable(veil-server
    server/main.cpp
    server/server_config.cpp
  )

  target_link_libraries(veil-server PRIVATE veil_common)
  veil_set_warnings(veil-server)

  install(TARGETS veil-server
    RUNTIME DESTINATION bin
  )
endif()

# GUI applications (optional, require Qt6)
option(VEIL_BUILD_GUI "Build Qt GUI applications" ON)

if(VEIL_BUILD_GUI)
  add_subdirectory(gui-client)
  if(VEIL_BUILD_SERVER)
    add_subdirectory(gui-server)
  endif()
endif()

# Windows Service executable (Windows only)
# The service provides IPC communication with the GUI, service management, and VPN tunnel functionality.
option(VEIL_BUILD_SERVICE "Build Windows service executable" ON)

if(WIN32 AND VEIL_BUILD_SERVICE)
  add_executable(veil-service
    windows/service_main.cpp
    windows/service_manager.cpp
  )
  target_link_libraries(veil-service PRIVATE veil_common)
  veil_set_warnings(veil-service)

  install(TARGETS veil-service
    RUNTIME DESTINATION bin
  )
endif()
