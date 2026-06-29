#pragma once
#include <string>
#include <vector>

namespace draind {

// Valid values: "", "none", "suspend", "hibernate", "hybrid-sleep", "poweroff"
using HwAction = std::string;

struct Profile {
    std::string name;
    std::string cpu_governor;
    std::string cpu_epp;
    int         brightness_percent     = 100;
    int         dim_brightness_percent = 20;
    int         dim_timeout            = 180; // seconds; 0 = disabled
    int         screen_off_timeout     = 300; // seconds; 0 = disabled
    int         sleep_timeout          = 600; // seconds; 0 = disabled
    HwAction    lid_close_action       = "suspend";
    HwAction    power_button_action    = "suspend";
    HwAction    sleep_button_action    = "suspend";
};

struct Config {
    std::string          active_profile;
    std::vector<Profile> profiles;

    const Profile* find(const std::string& name) const;
    const Profile* active_prof() const { return find(active_profile); }
};

// Throws std::runtime_error on parse failure.
Config load_config(const std::string& path);

} // namespace draind
