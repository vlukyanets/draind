#include "power_manager.hpp"
#include "../shared/logger.hpp"

#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>

namespace draind::daemon {

static bool write_file(const std::string& path, const std::string& value) {
    std::ofstream f(path);
    if (!f) {
        LOG_WARN << "write_file: cannot open " << path;
        return false;
    }
    f << value;
    return !f.fail();
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    return s;
}

std::vector<std::string> PowerManager::cpu_policy_paths() {
    std::vector<std::string> paths;
    DIR*                     dir = opendir("/sys/devices/system/cpu/cpufreq");
    if (!dir)
        return paths;
    dirent* de;
    while ((de = readdir(dir)))
        if (strncmp(de->d_name, "policy", 6) == 0)
            paths.push_back(std::string("/sys/devices/system/cpu/cpufreq/") + de->d_name);
    closedir(dir);
    return paths;
}

std::string PowerManager::find_backlight_path() {
    DIR* dir = opendir("/sys/class/backlight");
    if (!dir)
        return {};
    std::string path;
    dirent*     de;
    while ((de = readdir(dir))) {
        if (de->d_name[0] == '.')
            continue;
        path = std::string("/sys/class/backlight/") + de->d_name;
        break;
    }
    closedir(dir);
    return path;
}

bool PowerManager::set_brightness_percent(int pct) {
    std::string base = find_backlight_path();
    if (base.empty()) {
        LOG_WARN << "backlight: no sysfs node found";
        return false;
    }
    std::string max_s = read_file(base + "/max_brightness");
    if (max_s.empty()) {
        LOG_WARN << "backlight: cannot read max_brightness";
        return false;
    }
    int max_val = std::stoi(max_s);
    int target  = max_val * pct / 100;
    LOG_DEBUG << "brightness: " << pct << "% → " << target << "/" << max_val;
    return write_file(base + "/brightness", std::to_string(target));
}

int PowerManager::get_brightness_percent() const {
    std::string base = find_backlight_path();
    if (base.empty()) return -1;
    std::string cur_s = read_file(base + "/brightness");
    std::string max_s = read_file(base + "/max_brightness");
    if (cur_s.empty() || max_s.empty()) return -1;
    int max_val = std::stoi(max_s);
    if (max_val <= 0) return -1;
    return std::stoi(cur_s) * 100 / max_val;
}

bool PowerManager::set_cpu_governor(const std::string& gov) {
    if (gov.empty())
        return true;
    bool ok = true;
    for (const auto& p : cpu_policy_paths()) {
        std::string node = p + "/scaling_governor";
        std::string cur  = read_file(node);
        if (cur == gov)
            continue;
        if (!write_file(node, gov))
            ok = false;
        else
            LOG_DEBUG << "cpu_governor: " << cur << " → " << gov << " (" << p << ")";
    }
    if (ok)
        LOG_INFO << "cpu_governor → " << gov;
    return ok;
}

bool PowerManager::set_cpu_epp(const std::string& epp) {
    if (epp.empty())
        return true;
    bool ok = true;
    for (const auto& p : cpu_policy_paths()) {
        std::string node = p + "/energy_performance_preference";
        std::string cur  = read_file(node);
        if (cur == epp)
            continue;
        if (!write_file(node, epp))
            ok = false;
        else
            LOG_DEBUG << "cpu_epp: " << cur << " → " << epp << " (" << p << ")";
    }
    if (ok)
        LOG_INFO << "cpu_epp → " << epp;
    return ok;
}

void PowerManager::run_command(const std::string& cmd) {
    if (cmd.empty())
        return;
    LOG_DEBUG << "run_command: " << cmd;
    int r = system(cmd.c_str());
    if (r != 0)
        LOG_WARN << "command exited " << r << ": " << cmd;
}

void PowerManager::apply(const Profile& p) {
    LOG_INFO << "applying profile: " << p.name;
    set_cpu_governor(p.cpu_governor);
    set_cpu_epp(p.cpu_epp);
    set_brightness_percent(p.brightness_percent);
}

void PowerManager::dim(const Profile& p) {
    m_user_brightness = get_brightness_percent();
    if (m_user_brightness < 0)
        m_user_brightness = p.brightness_percent;
    LOG_INFO << "dimming to " << p.dim_brightness_percent
             << "% (user was at " << m_user_brightness << "%)";
    set_brightness_percent(p.dim_brightness_percent);
}

void PowerManager::undim(const Profile& p) {
    int restore = (m_user_brightness >= 0) ? m_user_brightness : p.brightness_percent;
    LOG_INFO << "restoring brightness to " << restore << "%";
    set_brightness_percent(restore);
    m_user_brightness = -1;
}

} // namespace draind::daemon
