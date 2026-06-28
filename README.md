# draind

Power management daemon for Linux/Wayland laptops.

Handles screen dimming, suspend-on-idle, and hardware button actions (lid, power, sleep) via configurable profiles. Works correctly with multiple logged-in users and supports both Wayland and non-Wayland setups.

## Features

- **Idle dimming and suspend** — configurable timeouts per profile
- **Wayland-native idle detection** — uses `ext_idle_notify_v1`, respects compositor inhibitors (e.g. fullscreen video)
- **MPRIS monitoring** — inhibits dim/sleep while any media player is playing
- **Hardware button handling** — lid close, power button, sleep button
- **Power profiles** — CPU governor, energy performance preference, brightness
- **Multi-user aware** — only the active session drives dim/sleep; inhibits from any session are respected
- **Fallback to `/dev/input`** — works without Wayland

## Architecture

```
draind  (root, system service)
  └── Unix socket /run/draind/draind.sock
        ├── draind-agent  (per-user, Wayland idle + MPRIS monitoring)
        └── draind-ctl    (one-shot CLI)
```

See [docs/architecture.md](docs/architecture.md) for details.

## Installation

### Arch Linux (AUR)

```sh
yay -S draind
```

### From source

**Dependencies:** `libsystemd`, `wayland-client` (optional), `wayland-protocols` (optional)

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
sudo ninja -C build install
```

After installing, follow the configuration steps below before starting the services.

## Configuration

draind has two config files: one for the system daemon (root), one per user for the agent.

### 1. Daemon — `/etc/draind/draind.json`

Controls power profiles: CPU governor, brightness, idle timeouts, and hardware button behaviour. The default file installed at `/etc/draind/draind.json` is a reasonable starting point — adjust timeouts and brightness values for your hardware.

#### Profile fields

| Field | Description | Default |
|---|---|---|
| `cpu_governor` | Kernel CPU governor | — |
| `cpu_epp` | Energy performance preference | — |
| `brightness_percent` | Screen brightness (%) | `100` |
| `dim_brightness_percent` | Brightness when dimmed (%) | `20` |
| `dim_timeout` | Idle time before dim (seconds) | `300` |
| `sleep_timeout` | Idle time before suspend (seconds, `0` = disabled) | `600` |
| `lid_close_action` | Action on lid close | `suspend` |
| `power_button_action` | Action on power button | `poweroff` |
| `sleep_button_action` | Action on sleep button | `suspend` |

**Hardware action values:** `none`, `suspend`, `hibernate`, `hybrid-sleep`, `poweroff`

Apply changes without restarting:

```sh
draind-ctl reload-config
```

### 2. Agent — `~/.config/draind/draind-agent.json`

Controls per-user, per-session behaviour. Create this file for each user who will use draind. If it does not exist, the system default at `/etc/xdg/draind/draind-agent.json` is used (empty commands — no lock, no pre-sleep hook).

#### Agent fields

| Field | Description |
|---|---|
| `lock_cmd` | Shell command to lock the screen before suspend |
| `before_sleep_cmd` | Shell command run just before suspend (synchronous; suspend waits for it) |

#### Example

```json
{
  "lock_cmd": "loginctl lock-session",
  "before_sleep_cmd": ""
}
```

`lock_cmd` only runs for the active session (the one at the seat). `before_sleep_cmd` runs for all logged-in users before the system suspends.

### Example daemon config

```json
{
  "default_profile": "balanced",
  "profiles": [
    {
      "name": "balanced",
      "cpu_governor": "powersave",
      "cpu_epp": "balance_performance",
      "brightness_percent": 70,
      "dim_brightness_percent": 10,
      "dim_timeout": 180,
      "sleep_timeout": 300,
      "lid_close_action": "suspend",
      "power_button_action": "poweroff",
      "sleep_button_action": "suspend"
    }
  ]
}
```

## Usage

```sh
# Enable and start services (done automatically on AUR install):
sudo systemctl enable --now draind
systemctl --user enable --now draind-agent

# CLI
draind-ctl status
draind-ctl list-profiles
draind-ctl set-profile powersave
draind-ctl reload-config
```

## Socket protocol

See [docs/protocol.md](docs/protocol.md).

## License

MIT — see [LICENSE](LICENSE).
