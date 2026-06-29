#pragma once
#include "../shared/config.hpp"
#include <functional>
#include <string>

namespace draind::daemon {

class ConfigManager {
  public:
    explicit ConfigManager(const std::string& path);

    // Load or reload. Throws on parse error.
    void reload();

    const Config&  config() const { return m_config; }
    const Profile* active_profile() const { return m_config.find(m_active); }
    std::string    active_profile_name() const { return m_active; }

    // Returns false if name not found.
    bool set_active(const std::string& name);

    void on_change(std::function<void(const Config&)> cb) { m_on_change = std::move(cb); }

  private:
    void persist_active();

    std::string                        m_path;
    Config                             m_config;
    std::string                        m_active;
    std::function<void(const Config&)> m_on_change;
};

} // namespace draind::daemon
