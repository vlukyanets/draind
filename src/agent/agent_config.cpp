#include "agent_config.hpp"
#include "../shared/json.hpp"

#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace draind::agent {

std::optional<AgentConfig> load_agent_config(const std::string& path) {
    std::ifstream f(path);
    if (!f)
        return std::nullopt;

    std::ostringstream ss;
    ss << f.rdbuf();

    json::Value root;
    try {
        root = json::parse(ss.str());
    } catch (const std::exception& e) {
        throw std::runtime_error("agent config parse error in " + path + ": " + e.what());
    }

    AgentConfig cfg;
    cfg.lock_cmd         = root.str("lock_cmd");
    cfg.before_sleep_cmd = root.str("before_sleep_cmd");
    return cfg;
}

} // namespace draind::agent
