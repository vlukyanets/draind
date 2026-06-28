#include "../shared/logger.hpp"
#include "agent.hpp"
#include "agent_config.hpp"

#include <cstdlib>
#include <iostream>
#include <pwd.h>
#include <stdexcept>
#include <unistd.h>

static std::string env_str(const char* key, const char* def = "") {
    const char* v = getenv(key);
    return v ? v : def;
}

static std::string user_config_path() {
    std::string base = env_str("XDG_CONFIG_HOME");
    if (base.empty()) {
        base = env_str("HOME");
        if (base.empty()) {
            struct passwd* pw = getpwuid(getuid());
            if (pw)
                base = pw->pw_dir;
        }
        base += "/.config";
    }
    return base + "/draind/draind-agent.json";
}

static constexpr const char* SYSTEM_AGENT_CONFIG = "/etc/xdg/draind/draind-agent.json";

int main(int argc, char** argv) {
    draind::agent::AgentOptions opts;
    opts.uid        = (uint32_t)getuid();
    opts.session_id = env_str("XDG_SESSION_ID");

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "-s" || a == "--socket") && i + 1 < argc) {
            opts.socket_path = argv[++i];
        } else if ((a == "-l" || a == "--log-level") && i + 1 < argc) {
            try {
                opts.log_level = draind::parse_log_level(argv[++i]);
            } catch (const std::exception& e) {
                std::cerr << "invalid log level: " << e.what() << "\n";
                return 1;
            }
        } else {
            std::cerr << "Usage: " << argv[0] << " [-s <socket>] [-l debug|info|warn|error]\n";
            return 1;
        }
    }

    if (getuid() == 0) {
        LOG_ERROR << "draind-agent must not be run as root";
        return 1;
    }

    if (opts.session_id.empty())
        LOG_WARN << "agent: XDG_SESSION_ID not set — session tracking will be limited";

    try {
        // User config takes priority; fall back to system default if absent.
        std::string user_cfg = user_config_path();
        auto        acfg     = draind::agent::load_agent_config(user_cfg);
        if (acfg) {
            LOG_INFO << "agent: config loaded from " << user_cfg;
        } else {
            acfg = draind::agent::load_agent_config(SYSTEM_AGENT_CONFIG);
            if (acfg)
                LOG_INFO << "agent: config loaded from " << SYSTEM_AGENT_CONFIG;
        }
        if (acfg) {
            opts.lock_cmd         = acfg->lock_cmd;
            opts.before_sleep_cmd = acfg->before_sleep_cmd;
        }
    } catch (const std::exception& e) {
        LOG_WARN << "agent: " << e.what() << " — continuing with defaults";
    }

    draind::agent::Agent agent(opts);
    return agent.run();
}
