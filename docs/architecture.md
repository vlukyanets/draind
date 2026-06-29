# Architecture

## Overview

draind splits responsibility across two long-running processes:

- **draind** (root) — policy engine. Knows the active session, owns timers, writes sysfs.
- **draind-agent** (user) — sensor. Knows what the user is doing in their session.

This split exists because sysfs writes require root, while Wayland and the session D-Bus
are per-user and reject root connections.

## draind (root daemon)

Responsibilities:
- Load and reload config (`/etc/draind/draind.json`)
- Write backlight brightness (`/sys/class/backlight/*/brightness`)
- Write CPU governor and EPP (`/sys/devices/system/cpu/cpufreq/*/`)
- Track the active logind session on `seat0` via system D-Bus
- Accept agent connections; only honour signals from the active session's agent
- Accept ctl connections for status queries and profile changes
- Manage dim, screen-off, and sleep policy; execute suspend

The daemon has no Wayland or session D-Bus connection. It is intentionally blind to
per-session state except through what agents report.

## draind-agent (per-user)

Responsibilities:
- Connect to the daemon socket and register with session ID
- Monitor Wayland idle via `ext_idle_notify_v1` (respects compositor idle inhibitors)
- Control display power via `zwlr_output_power_manager_v1` (DPMS on/off)
- Monitor MPRIS2 players on the session D-Bus for active media playback
- Send `idle_dim`, `idle_screen_off`, `idle_sleep`, `active` events to the daemon
- Send `inhibit`/`uninhibit` when media playback starts/stops (belt-and-suspenders
  in case the compositor does not bridge D-Bus screensaver inhibitors to Wayland idle)
- Run the user-configured `lock_cmd` when the daemon sends a `lock` message
- Turn displays off/on when the daemon sends a `screen_off` message

One agent instance runs per logged-in user. It is started by the compositor
(e.g. via `spawn-at-startup` in niri's `config.kdl`) so it inherits the full
compositor environment (`WAYLAND_DISPLAY`, `NIRI_SOCKET`, `XDG_SESSION_ID`, etc.)
from the moment it starts.

## Multi-user behaviour

When two users are logged in:
- Two agent instances run simultaneously, each registered with their logind session ID
- The daemon queries logind for the active session on `seat0`
- Only the active session's agent can trigger dim/screen-off/sleep
- If the active session switches (fast user switching), the daemon immediately begins
  honouring the new active agent's signals
- An inhibit from any agent still suppresses dim/sleep (e.g. a background user is
  transcoding; no sleep)

## Session lifecycle

```
user logs in
  → systemd starts draind-agent
  → agent connects to /run/draind/draind.sock
  → agent sends HELLO {session_id, uid}
  → daemon records agent, checks if session is active

user is idle (no input, no media)
  → Wayland compositor fires ext_idle_notify idled event at dim_timeout
  → agent sends IDLE_DIM to daemon
  → daemon checks: is this agent's session active? yes
  → daemon dims backlight

  → compositor fires idled event at screen_off_timeout
  → agent sends IDLE_SCREEN_OFF to daemon
  → daemon (if no inhibitors) sends SCREEN_OFF to agent
  → agent calls zwlr_output_power_manager_v1 → compositor turns displays off

user moves mouse
  → Wayland compositor fires resumed event
  → agent turns displays on via zwlr_output_power_manager_v1
  → agent sends ACTIVE to daemon
  → daemon restores backlight

sleep timeout expires
  → agent sends IDLE_SLEEP to daemon
  → daemon sends LOCK to the active session's agent (fire-and-forget)
  → daemon broadcasts PRE_SLEEP to all agents; each runs before_sleep_cmd and acks
  → daemon calls systemctl suspend

user logs out
  → agent disconnects (daemon drops its record)
```

## Config

The daemon reads `/etc/draind/draind.json` (system-wide, root-owned).

Each user's agent reads `$XDG_CONFIG_HOME/draind/draind-agent.json`
(typically `~/.config/draind/draind-agent.json`).
If absent, the system default `/etc/xdg/draind/draind-agent.json` is used.

```json
{
  "lock_cmd":         "bash -c '~/.config/niri/switch-layout.sh 0; qs -c noctalia-shell ipc call lockScreen lock'",
  "before_sleep_cmd": "notify-send 'Going to sleep'"
}
```

| Key | Runs as | When | Notes |
|-----|---------|------|-------|
| `lock_cmd` | user | on lock/pre-suspend | active session only; forked (non-blocking) |
| `before_sleep_cmd` | user | pre-suspend | all sessions; synchronous, acked before suspend |

Both commands are run via `/bin/sh -c` in a forked child and inherit the agent's
environment. Because the agent is started by the compositor, this environment
already contains `NIRI_SOCKET`, `WAYLAND_DISPLAY`, and other session variables.
