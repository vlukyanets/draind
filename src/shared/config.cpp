#include "config.hpp"
#include "json.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace draind {

const Profile* Config::find(const std::string& name) const {
    for (const auto& p : profiles)
        if (p.name == name)
            return &p;
    return nullptr;
}

Config load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("cannot open config: " + path);

    std::ostringstream ss;
    ss << f.rdbuf();
    json::Value root = json::parse(ss.str());

    Config cfg;
    cfg.default_profile = root.str("default_profile");

    for (const auto& pv : root["profiles"].get_array()) {
        Profile p;
        p.name                   = pv.str("name");
        p.cpu_governor           = pv.str("cpu_governor");
        p.cpu_epp                = pv.str("cpu_epp");
        p.brightness_percent     = (int)pv.num("brightness_percent", 100);
        p.dim_brightness_percent = (int)pv.num("dim_brightness_percent", 20);
        p.dim_timeout            = (int)pv.num("dim_timeout", 300);
        p.sleep_timeout          = (int)pv.num("sleep_timeout", 600);
        p.lid_close_action       = pv.str("lid_close_action", "suspend");
        p.power_button_action    = pv.str("power_button_action", "poweroff");
        p.sleep_button_action    = pv.str("sleep_button_action", "suspend");

        if (p.name.empty())
            throw std::runtime_error("profile missing 'name'");
        cfg.profiles.push_back(std::move(p));
    }

    if (cfg.profiles.empty())
        throw std::runtime_error("config has no profiles");
    if (cfg.default_profile.empty())
        cfg.default_profile = cfg.profiles[0].name;

    return cfg;
}

} // namespace draind
