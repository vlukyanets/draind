#include "agent.hpp"
#include "../shared/logger.hpp"

#include <cstdlib>
#include <iostream>
#include <pwd.h>
#include <stdexcept>
#include <unistd.h>

static std::string env_str(const char* key, const char* def = "") {
    const char* v = getenv(key);
    return v ? v : def;
}

int main(int argc, char** argv) {
    draind::AgentOptions opts;
    opts.uid        = (uint32_t)getuid();
    opts.session_id = env_str("XDG_SESSION_ID");

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "-s" || a == "--socket") && i + 1 < argc) {
            opts.socket_path = argv[++i];
        } else if ((a == "-l" || a == "--log-level") && i + 1 < argc) {
            try { opts.log_level = draind::parse_log_level(argv[++i]); }
            catch (const std::exception& e) {
                std::cerr << "invalid log level: " << e.what() << "\n";
                return 1;
            }
        } else {
            std::cerr << "Usage: " << argv[0]
                      << " [-s <socket>] [-l debug|info|warn|error]\n";
            return 1;
        }
    }

    if (getuid() == 0) {
        LOG_ERROR << "draind-agent must not be run as root";
        return 1;
    }

    if (opts.session_id.empty())
        LOG_WARN << "agent: XDG_SESSION_ID not set — session tracking will be limited";

    draind::Agent agent(opts);
    return agent.run();
}
