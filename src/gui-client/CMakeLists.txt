# VEIL Client GUI Application (Qt/C++)

find_package(Qt6 COMPONENTS Core Widgets Network Svg QUIET)

if(NOT Qt6_FOUND)
  message(STATUS "Qt6 not found, client GUI will not be built")
  message(STATUS "To build the GUI, install Qt6: sudo apt-get install qt6-base-dev")
  return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(veil-client-gui
  main.cpp
  mainwindow.cpp
  mainwindow.h
  connection_widget.cpp
  connection_widget.h
  settings_widget.cpp
  settings_widget.h
  collapsible_section.cpp
  collapsible_section.h
  diagnostics_widget.cpp
  diagnostics_widget.h
  statistics_widget.cpp
  statistics_widget.h
  ipc_client_manager.cpp
  ipc_client_manager.h
  notification_preferences.cpp
  notification_preferences.h
  notification_history_dialog.cpp
  notification_history_dialog.h
  update_checker.cpp
  update_checker.h
  setup_wizard.cpp
  setup_wizard.h
  server_config.cpp
  server_config.h
  server_list_widget.cpp
  server_list_widget.h
  server_selector_widget.cpp
  server_selector_widget.h
  quick_actions_widget.cpp
  quick_actions_widget.h
  app_split_tunnel_widget.cpp
  app_split_tunnel_widget.h
  resources/resources.qrc
)

target_link_libraries(veil-client-gui
  PRIVATE
    veil_common
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    $<$<TARGET_EXISTS:Qt6::Svg>:Qt6::Svg>
)

veil_set_warnings(veil-client-gui)

install(TARGETS veil-client-gui
  RUNTIME DESTINATION bin
)
