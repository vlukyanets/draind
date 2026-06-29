#pragma once
// Read battery state from /sys/class/power_supply/BAT*.
// All fields are best-effort: absent sysfs files leave them at their defaults.

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>

namespace draind::battery {

enum class Status { Unknown, Discharging, Charging, Full, NotCharging, Absent };

struct BatteryInfo {
    bool             present    = false;
    std::string      name;          // e.g. "BAT1"
    int              percent    = -1;
    Status           status     = Status::Unknown;
    std::optional<int> time_to_empty_min; // minutes until empty (discharging)
    std::optional<int> time_to_full_min;  // minutes until full (charging)
};

inline Status parse_status(const std::string& s) {
    if (s == "Discharging")  return Status::Discharging;
    if (s == "Charging")     return Status::Charging;
    if (s == "Full")         return Status::Full;
    if (s == "Not charging") return Status::NotCharging;
    return Status::Unknown;
}

inline std::string status_string(Status s) {
    switch (s) {
    case Status::Discharging:  return "discharging";
    case Status::Charging:     return "charging";
    case Status::Full:         return "full";
    case Status::NotCharging:  return "not_charging";
    case Status::Absent:       return "absent";
    default:                   return "unknown";
    }
}

static inline bool read_int(const std::string& path, int64_t& out) {
    std::ifstream f(path);
    if (!f)
        return false;
    f >> out;
    return !f.fail();
}

static inline bool read_str(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f)
        return false;
    std::getline(f, out);
    return !out.empty();
}

// Returns info for the first BAT* supply found, or present=false if none.
inline BatteryInfo read() {
    for (int i = 0; i <= 9; ++i) {
        std::string base = "/sys/class/power_supply/BAT" + std::to_string(i);
        std::ifstream probe(base + "/present");
        if (!probe)
            continue;

        BatteryInfo info;
        info.present = true;
        info.name    = "BAT" + std::to_string(i);

        int64_t v;
        if (read_int(base + "/capacity", v))
            info.percent = (int)v;

        std::string st;
        if (read_str(base + "/status", st))
            info.status = parse_status(st);
        else
            info.status = Status::Absent;

        // Try energy-based (µWh / µW) path first, then charge-based (µAh / µA).
        // Both give the same formula: remaining / rate → hours.
        int64_t remaining = 0, total = 0, rate = 0;
        bool have_time = false;

        if (read_int(base + "/energy_now", remaining) &&
            read_int(base + "/energy_full", total) &&
            read_int(base + "/power_now", rate) && rate > 0) {
            have_time = true;
        } else if (read_int(base + "/charge_now", remaining) &&
                   read_int(base + "/charge_full", total) &&
                   read_int(base + "/current_now", rate) && rate > 0) {
            have_time = true;
        }

        if (have_time) {
            if (info.status == Status::Discharging && remaining > 0)
                info.time_to_empty_min = (int)(remaining * 60 / rate);
            else if (info.status == Status::Charging && total > remaining)
                info.time_to_full_min = (int)((total - remaining) * 60 / rate);
        }

        return info;
    }

    BatteryInfo absent;
    absent.status = Status::Absent;
    return absent;
}

} // namespace draind::battery
