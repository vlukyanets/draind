#include "config_manager.hpp"
#include "../shared/logger.hpp"

namespace draind::daemon {

ConfigManager::ConfigManager(std::string path) : m_path(std::move(path)) { reload(); }

void ConfigManager::reload() {
    LOG_DEBUG << "config: loading " << m_path;
    Config c = load_config(m_path);
    LOG_INFO << "config: loaded " << c.profiles.size() << " profile(s)"
             << ", default=" << c.default_profile;
    m_config = std::move(c);

    // Keep current active profile if it still exists; otherwise fall back to default.
    if (!m_active.empty() && m_config.find(m_active))
        ; // keep it
    else
        m_active = m_config.default_profile;

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
    if (m_on_change)
        m_on_change(m_config);
    return true;
}

} // namespace draind::daemon
