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

## Configuration

The config file is at `/etc/draind/draind.json`. Edit it to adjust timeouts and profile settings, then reload:

```sh
draind-ctl reload-config
```

### Profile fields

| Field | Description | Default |
|---|---|---|
| `cpu_governor` | Kernel CPU governor | — |
| `cpu_epp` | Energy performance preference | — |
| `brightness_percent` | Screen brightness (%) | `100` |
| `dim_brightness_percent` | Brightness when dimmed (%) | `20` |
| `dim_timeout` | Idle time before dim (seconds) | `300` |
| `sleep_timeout` | Idle time before suspend (seconds, `0` = disabled) | `600` |
| `before_sleep_cmd` | Command to run before suspend | — |
| `lid_close_action` | Action on lid close | `suspend` |
| `power_button_action` | Action on power button | `poweroff` |
| `sleep_button_action` | Action on sleep button | `suspend` |

**Hardware action values:** `none`, `suspend`, `hibernate`, `hybrid-sleep`, `poweroff`

### Example

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
# System daemon (enabled by pacman install hook)
systemctl enable --now draind

# Per-user agent
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
