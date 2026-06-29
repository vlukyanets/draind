# draind

Power management daemon for Linux/Wayland laptops.

## Architecture

```
┌─────────────────────────────────────────┐
│  draind  (root, system service)         │
│  - backlight & CPU sysfs writes         │
│  - dim/screen-off/sleep policy          │
│  - tracks active logind session         │
│  - Unix socket: /run/draind/draind.sock │
└────────┬──────────────────┬────────────┘
         │ agent protocol   │ ctl protocol
   ┌─────▼──────┐     ┌─────▼──────┐
   │draind-agent│ … N │ draind-ctl │
   │(user svc)  │     │ (one-shot) │
   │            │     └────────────┘
   │ Wayland    │
   │ idle notify│
   │ output pwr │
   │ MPRIS mon  │
   └────────────┘
```

One `draind-agent` instance runs per logged-in user (systemd user service).
The agent owns all per-session monitoring and tells the daemon when the session
is idle or inhibited. The daemon only acts on signals from the **active** session
(determined via logind). See [docs/architecture.md](docs/architecture.md).

## Components

| Path | Binary | Runs as |
|------|--------|---------|
| `src/daemon/` | `draind` | root (system service) |
| `src/agent/` | `draind-agent` | user (user service) |
| `src/ctl/` | `draind-ctl` | user (one-shot CLI) |
| `src/shared/` | — | shared headers/sources |

### Notable shared headers

| File | Purpose |
|------|---------|
| `src/shared/protocol.hpp` | All socket message encode/decode helpers |
| `src/shared/battery.hpp` | Read battery state from `/sys/class/power_supply/BAT*` |
| `src/shared/json.hpp` | Minimal JSON parser/serialiser |
| `src/shared/logger.hpp` | Lightweight logging macros |

### Notable agent sources

| File | Purpose |
|------|---------|
| `src/agent/wayland_idle_monitor.cpp` | `ext_idle_notify_v1` — idle/active callbacks |
| `src/agent/wayland_output_power.cpp` | `zwlr_output_power_manager_v1` — DPMS on/off |
| `src/agent/mpris_monitor.cpp` | D-Bus MPRIS2 — inhibit while media plays |

## Build

```sh
cmake -B build -G Ninja
ninja -C build
```

Dependencies: `libsystemd`, `wayland-client`, `wayland-scanner`.
Protocol XMLs are bundled in `protocols/`; no system `wayland-protocols` package required.

## Socket protocol

See [docs/protocol.md](docs/protocol.md). All messages are newline-terminated JSON.
Agent and ctl use the same socket; the daemon distinguishes them by the first message type.

## Conventions

- C++20, no exceptions across the daemon↔agent socket boundary
- No comment unless the WHY is non-obvious (hidden constraint, workaround, invariant)
- Each subsystem is a self-contained class; `Daemon` and `Agent` wire them together
- Policy (what to do when idle) lives in the daemon; observation (detecting idle) lives in the agent
- Wayland is a required dependency — there is no `/dev/input` fallback
