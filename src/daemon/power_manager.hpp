#pragma once
#include "../shared/config.hpp"
#include <string>
#include <vector>

namespace draind::daemon {

class PowerManager {
  public:
    void apply(const Profile& p); // set governor, EPP, brightness
    void dim(const Profile& p);   // lower brightness to dim_brightness_percent
    void undim(const Profile& p); // restore brightness to brightness_percent

    static void run_command(const std::string& cmd);

  private:
    bool set_brightness_percent(int pct);
    bool set_cpu_governor(const std::string& gov);
    bool set_cpu_epp(const std::string& epp);

    static std::vector<std::string> cpu_policy_paths();
    static std::string              find_backlight_path();
};

} // namespace draind::daemon
