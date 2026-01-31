# Platform-specific test sources
if(WIN32)
  # Transport layer tests are now available on Windows
  set(VEIL_PLATFORM_TEST_SOURCES
    udp_socket_tests.cpp
    ack_bitmap_tests.cpp
    reorder_buffer_tests.cpp
    fragment_reassembly_tests.cpp
    mux_codec_tests.cpp
    retransmit_buffer_tests.cpp
    ack_scheduler_tests.cpp
    congestion_controller_tests.cpp
    transport_session_tests.cpp
    session_migration_tests.cpp
    console_handler_tests.cpp
    service_manager_tests.cpp
    app_enumerator_tests.cpp
    shortcut_manager_tests.cpp
  )
else()
  set(VEIL_PLATFORM_TEST_SOURCES
    config_tests.cpp
    udp_socket_tests.cpp
    ack_bitmap_tests.cpp
    reorder_buffer_tests.cpp
    fragment_reassembly_tests.cpp
    mux_codec_tests.cpp
    retransmit_buffer_tests.cpp
    ack_scheduler_tests.cpp
    congestion_controller_tests.cpp
    transport_session_tests.cpp
    signal_handler_tests.cpp
    daemon_tests.cpp
    session_table_tests.cpp
    session_migration_tests.cpp
    service_manager_tests.cpp
  )
endif()

add_executable(veil_unit_tests
  random_tests.cpp
  crypto_tests.cpp
  hardware_crypto_tests.cpp
  websocket_wrapper_tests.cpp
  http_handshake_emulator_tests.cpp
  tls_wrapper_tests.cpp
  packet_tests.cpp
  replay_window_tests.cpp
  handshake_tests.cpp
  handshake_replay_cache_tests.cpp
  client_registry_tests.cpp
  multi_client_handshake_tests.cpp
  session_ticket_tests.cpp
  zero_rtt_handshake_tests.cpp
  timer_heap_tests.cpp
  obfuscation_tests.cpp
  tun_device_tests.cpp
  routing_tests.cpp
  mtu_discovery_tests.cpp
  advanced_rate_limiter_tests.cpp
  session_lifecycle_tests.cpp
  constrained_logging_tests.cpp
  metrics_tests.cpp
  thread_checker_tests.cpp
  spsc_queue_tests.cpp
  thread_pool_tests.cpp
  packet_pool_tests.cpp
  error_message_tests.cpp
  auto_updater_tests.cpp
  ipc_protocol_tests.cpp
  ${VEIL_PLATFORM_TEST_SOURCES}
)

target_link_libraries(veil_unit_tests PRIVATE
  veil_common
  GTest::gtest_main
)

veil_set_warnings(veil_unit_tests)

include(GoogleTest)
gtest_discover_tests(veil_unit_tests PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

# GUI widget tests (requires Qt6)
find_package(Qt6 COMPONENTS Core Widgets Network Test QUIET)
if(Qt6_FOUND)
  set(CMAKE_AUTOMOC ON)

  add_executable(veil_gui_tests
    setup_wizard_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/setup_wizard.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/setup_wizard.h
    statistics_widget_tests.cpp
    quick_actions_widget_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/statistics_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/statistics_widget.h
    ${CMAKE_SOURCE_DIR}/src/gui-client/quick_actions_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/quick_actions_widget.h
    connection_widget_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/connection_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/connection_widget.h
    ${CMAKE_SOURCE_DIR}/src/gui-client/server_selector_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/server_selector_widget.h
    ipc_client_manager_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/ipc_client_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/ipc_client_manager.h
    settings_widget_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/settings_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/settings_widget.h
    ${CMAKE_SOURCE_DIR}/src/gui-client/collapsible_section.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/collapsible_section.h
    ${CMAKE_SOURCE_DIR}/src/gui-client/app_split_tunnel_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/app_split_tunnel_widget.h
    ${CMAKE_SOURCE_DIR}/src/gui-client/server_config.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/server_config.h
    ${CMAKE_SOURCE_DIR}/src/gui-client/notification_preferences.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/notification_preferences.h
    ${CMAKE_SOURCE_DIR}/src/gui-client/notification_history_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/gui-client/notification_history_dialog.h
  )

  target_include_directories(veil_gui_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/gui-client
    ${CMAKE_SOURCE_DIR}/src
  )

  target_link_libraries(veil_gui_tests PRIVATE
    veil_common
    GTest::gtest_main
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Test
  )

  veil_set_warnings(veil_gui_tests)

  # QT_QPA_PLATFORM=offscreen is needed for headless CI servers.
  # DISCOVERY_MODE PRE_TEST defers test discovery to ctest run time,
  # where the ENVIRONMENT property is honored.
  gtest_discover_tests(veil_gui_tests
    PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0;QT_QPA_PLATFORM=offscreen"
    DISCOVERY_MODE PRE_TEST
  )
else()
  message(STATUS "Qt6 not found, GUI widget tests will not be built")
endif()
