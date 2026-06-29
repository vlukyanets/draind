# draind

Power management daemon for Linux/Wayland laptops.

## Architecture

```
┌─────────────────────────────────────────┐
│  draind  (root, system service)         │
│  - backlight & CPU sysfs writes         │
│  - dim/sleep policy and timers          │
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

## Build

```sh
cmake -B build -G Ninja
ninja -C build
```

Dependencies: `libsystemd`, `wayland-client`, `wayland-protocols`, `wayland-scanner`.

## Socket protocol

See [docs/protocol.md](docs/protocol.md). All messages are newline-terminated JSON.
Agent and ctl use the same socket; the daemon distinguishes them by the first message type.

## Conventions

- C++20, no exceptions across the daemon↔agent socket boundary
- No comment unless the WHY is non-obvious (hidden constraint, workaround, invariant)
- Each subsystem is a self-contained class; `Daemon` and `Agent` wire them together
- Policy (what to do when idle) lives in the daemon; observation (detecting idle) lives in the agent
## Wayland fallback — mandatory

The agent must start and function correctly in all of these situations:

| Build | Runtime | Expected behaviour |
|-------|---------|-------------------|
| `HAVE_WAYLAND` defined | `WAYLAND_DISPLAY` set, compositor supports `ext_idle_notify_v1` | Full Wayland idle path |
| `HAVE_WAYLAND` defined | `WAYLAND_DISPLAY` unset or connection fails | Silent fallback to `/dev/input` |
| `HAVE_WAYLAND` defined | Compositor lacks `ext_idle_notify_v1` | Silent fallback to `/dev/input` |
| `HAVE_WAYLAND` not defined | any | `/dev/input` only, no Wayland code compiled |

**Rule**: `wayland_idle_monitor.init()` returning `false` is never an error — the agent continues with `input_idle_monitor` as the idle source. No log level above `INFO` is emitted for a missing Wayland display. The agent never exits due to missing Wayland.
