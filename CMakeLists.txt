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

# ── Optional Wayland support ──────────────────────────────────────────────────
# When absent the agent falls back to /dev/input idle monitoring.
pkg_check_modules(WAYLAND_CLIENT wayland-client)

if(WAYLAND_CLIENT_FOUND)
    find_program(WAYLAND_SCANNER wayland-scanner)
    find_file(EXT_IDLE_NOTIFY_XML
        NAMES ext-idle-notify-v1.xml
        PATHS /usr/share/wayland-protocols/staging/ext-idle-notify)

    if(WAYLAND_SCANNER AND EXT_IDLE_NOTIFY_XML)
        set(HAVE_WAYLAND TRUE)
    else()
        message(WARNING
            "wayland-client found but wayland-scanner or ext-idle-notify-v1.xml "
            "is missing — building without Wayland idle support")
    endif()
endif()

if(HAVE_WAYLAND)
    message(STATUS "Wayland idle support: enabled")

    set(PROTO_H ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.h)
    set(PROTO_C ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1.c)

    add_custom_command(OUTPUT ${PROTO_H} ${PROTO_C}
        COMMAND ${WAYLAND_SCANNER} client-header ${EXT_IDLE_NOTIFY_XML} ${PROTO_H}
        COMMAND ${WAYLAND_SCANNER} private-code   ${EXT_IDLE_NOTIFY_XML} ${PROTO_C}
        DEPENDS ${EXT_IDLE_NOTIFY_XML}
        COMMENT "Generating ext-idle-notify-v1 protocol glue")

    add_custom_target(wayland_proto DEPENDS ${PROTO_H} ${PROTO_C})
else()
    message(STATUS "Wayland idle support: disabled (using /dev/input fallback)")
endif()

# ── Shared sources ────────────────────────────────────────────────────────────
set(SHARED_SOURCES
    src/shared/config.cpp
    src/shared/socket.cpp
)

# ── draind (root daemon) ──────────────────────────────────────────────────────
add_executable(draind
    src/daemon/main.cpp
    src/daemon/daemon.cpp
    src/daemon/power_manager.cpp
    src/daemon/config_manager.cpp
    src/daemon/session_tracker.cpp
    src/daemon/server.cpp
    src/daemon/hw_event_monitor.cpp
    ${SHARED_SOURCES}
)
target_include_directories(draind PRIVATE src ${SYSTEMD_INCLUDE_DIR})
target_link_libraries(draind PRIVATE ${SYSTEMD_LIB})

# ── draind-agent (per-user) ───────────────────────────────────────────────────
set(AGENT_SOURCES
    src/agent/main.cpp
    src/agent/agent.cpp
    src/agent/mpris_monitor.cpp
    src/agent/input_idle_monitor.cpp
    ${SHARED_SOURCES}
)

if(HAVE_WAYLAND)
    list(APPEND AGENT_SOURCES src/agent/wayland_idle_monitor.cpp ${PROTO_C})
endif()

add_executable(draind-agent ${AGENT_SOURCES})

if(HAVE_WAYLAND)
    add_dependencies(draind-agent wayland_proto)
    target_compile_definitions(draind-agent PRIVATE HAVE_WAYLAND)
    target_include_directories(draind-agent PRIVATE
        ${WAYLAND_CLIENT_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(draind-agent PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
endif()

target_include_directories(draind-agent PRIVATE src ${SYSTEMD_INCLUDE_DIR})
target_link_libraries(draind-agent PRIVATE ${SYSTEMD_LIB})

# ── draind-ctl (CLI) ──────────────────────────────────────────────────────────
add_executable(draind-ctl
    src/ctl/main.cpp
    ${SHARED_SOURCES}
)
target_include_directories(draind-ctl PRIVATE src)

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS draind draind-agent draind-ctl
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/draind.json
    DESTINATION /etc/draind)
install(FILES config/draind.service
    DESTINATION /usr/lib/systemd/system)
install(FILES config/draind-agent.service
    DESTINATION /usr/lib/systemd/user)
install(FILES config/draind-sysusers.conf
    DESTINATION /usr/lib/sysusers.d RENAME draind.conf)
install(FILES config/logind-draind.conf
    DESTINATION /usr/lib/systemd/logind.conf.d RENAME draind.conf)
