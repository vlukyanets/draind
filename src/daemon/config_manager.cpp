#include "config_manager.hpp"
#include "../shared/logger.hpp"

#include "../shared/json.hpp"

#include <fstream>

namespace draind::daemon {

ConfigManager::ConfigManager(std::string path) : m_path(std::move(path)) { reload(); }

void ConfigManager::reload() {
    LOG_DEBUG << "config: loading " << m_path;
    Config c = load_config(m_path);
    m_config = std::move(c);
    m_active = m_config.active_profile;
    LOG_INFO << "config: loaded " << m_config.profiles.size() << " profile(s)"
             << ", active=" << m_active;
    if (m_on_change)
        m_on_change(m_config);
}

bool ConfigManager::set_active(const std::string& name) {
    if (!m_config.find(name)) {
        LOG_WARN << "config: profile not found: " << name;
        return false;
    }
    m_active = name;
    LOG_INFO << "config: active profile → " << name;
    persist_active();
    if (m_on_change)
        m_on_change(m_config);
    return true;
}

void ConfigManager::persist_active() {
    std::ifstream in(m_path);
    if (!in) {
        LOG_WARN << "config: cannot read " << m_path << " for update";
        return;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    json::Value root;
    try {
        root = json::parse(text);
    } catch (const std::exception& e) {
        LOG_WARN << "config: cannot parse " << m_path << " for update: " << e.what();
        return;
    }

    root["active_profile"] = m_active;

    std::ofstream out(m_path, std::ios::trunc);
    if (!out) {
        LOG_WARN << "config: cannot write " << m_path;
        return;
    }
    out << json::dump_pretty(root);
    LOG_DEBUG << "config: persisted active_profile=" << m_active;
}

} // namespace draind::daemon
