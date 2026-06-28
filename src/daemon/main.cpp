#include "../shared/logger.hpp"
#include "daemon.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

static void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [-f] [-c <config>] [-l debug|info|warn|error]\n";
}

int main(int argc, char** argv) {
    draind::daemon::DaemonOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-f" || a == "--foreground") {
            opts.foreground = true;
        } else if ((a == "-c" || a == "--config") && i + 1 < argc) {
            opts.config_path = argv[++i];
        } else if ((a == "-l" || a == "--log-level") && i + 1 < argc) {
            try {
                opts.log_level = draind::parse_log_level(argv[++i]);
            } catch (const std::exception& e) {
                std::cerr << "invalid log level: " << e.what() << "\n";
                return 1;
            }
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    draind::daemon::Daemon daemon(opts);
    return daemon.run();
}
