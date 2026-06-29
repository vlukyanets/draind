cmake_minimum_required(VERSION 3.20)
project(draind CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_definitions(_GNU_SOURCE)

# ── Required dependencies ─────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

find_library(SYSTEMD_LIB NAMES systemd REQUIRED)
find_path(SYSTEMD_INCLUDE_DIR NAMES systemd/sd-bus.h REQUIRED)

# ── Wayland (required) ────────────────────────────────────────────────────────
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protocols)

# ext-idle-notify-v1
set(EXT_IDLE_XML ${PROTO_DIR}/ext-idle-notify-v1.xml)
set(EXT_IDLE_H   ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.h)
set(EXT_IDLE_C   ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1.c)

add_custom_command(
  OUTPUT ${EXT_IDLE_H} ${EXT_IDLE_C}
  COMMAND ${WAYLAND_SCANNER} client-header ${EXT_IDLE_XML} ${EXT_IDLE_H}
  COMMAND ${WAYLAND_SCANNER} private-code   ${EXT_IDLE_XML} ${EXT_IDLE_C}
  DEPENDS ${EXT_IDLE_XML}
  COMMENT "Generating ext-idle-notify-v1 protocol glue")

# wlr-output-power-management-unstable-v1
set(OUTPUT_POWER_XML ${PROTO_DIR}/wlr-output-power-management-unstable-v1.xml)
set(OUTPUT_POWER_H   ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-client-protocol.h)
set(OUTPUT_POWER_C   ${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1.c)

add_custom_command(
  OUTPUT ${OUTPUT_POWER_H} ${OUTPUT_POWER_C}
  COMMAND ${WAYLAND_SCANNER} client-header ${OUTPUT_POWER_XML} ${OUTPUT_POWER_H}
  COMMAND ${WAYLAND_SCANNER} private-code   ${OUTPUT_POWER_XML} ${OUTPUT_POWER_C}
  DEPENDS ${OUTPUT_POWER_XML}
  COMMENT "Generating wlr-output-power-management-unstable-v1 protocol glue")

add_custom_target(wayland_proto DEPENDS ${EXT_IDLE_H} ${EXT_IDLE_C}
                                        ${OUTPUT_POWER_H} ${OUTPUT_POWER_C})

# ── Shared sources ────────────────────────────────────────────────────────────
set(SHARED_SOURCES src/shared/config.cpp src/shared/socket.cpp)

# ── draind (root daemon) ──────────────────────────────────────────────────────
add_executable(
  draind
  src/daemon/main.cpp
  src/daemon/daemon.cpp
  src/daemon/power_manager.cpp
  src/daemon/config_manager.cpp
  src/daemon/session_tracker.cpp
  src/daemon/server.cpp
  src/daemon/hw_event_monitor.cpp
  ${SHARED_SOURCES})
target_include_directories(draind PRIVATE src ${SYSTEMD_INCLUDE_DIR})
target_link_libraries(draind PRIVATE ${SYSTEMD_LIB})

# ── draind-agent (per-user) ───────────────────────────────────────────────────
add_executable(
  draind-agent
  src/agent/main.cpp
  src/agent/agent.cpp
  src/agent/agent_config.cpp
  src/agent/mpris_monitor.cpp
  src/agent/wayland_idle_monitor.cpp
  src/agent/wayland_output_power.cpp
  ${EXT_IDLE_C}
  ${OUTPUT_POWER_C}
  ${SHARED_SOURCES})

add_dependencies(draind-agent wayland_proto)
target_include_directories(draind-agent PRIVATE src
                                                ${WAYLAND_CLIENT_INCLUDE_DIRS}
                                                ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(draind-agent PRIVATE ${WAYLAND_CLIENT_LIBRARIES} ${SYSTEMD_LIB})

# ── draind-ctl (CLI) ──────────────────────────────────────────────────────────
add_executable(draind-ctl src/ctl/main.cpp ${SHARED_SOURCES})
target_include_directories(draind-ctl PRIVATE src)

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS draind draind-agent draind-ctl
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/draind.json DESTINATION /etc/draind)
install(FILES config/draind-agent.json DESTINATION /etc/xdg/draind)
install(FILES config/draind.service DESTINATION /usr/lib/systemd/system)
install(
  FILES config/draind-sysusers.conf
  DESTINATION /usr/lib/sysusers.d
  RENAME draind.conf)
install(
  FILES config/logind-draind.conf
  DESTINATION /usr/lib/systemd/logind.conf.d
  RENAME draind.conf)
