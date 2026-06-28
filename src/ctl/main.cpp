#include "../shared/json.hpp"
#include "../shared/logger.hpp"
#include "../shared/protocol.hpp"
#include "../shared/socket.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <unistd.h>

using namespace draind;

static void usage(const char* argv0) {
    std::cerr << "Usage:\n"
              << "  " << argv0 << " status\n"
              << "  " << argv0 << " list-profiles\n"
              << "  " << argv0 << " list-inhibitors\n"
              << "  " << argv0 << " set-profile <name>\n"
              << "  " << argv0 << " reload-config\n"
              << "  " << argv0 << " lock\n"
              << "\nOptions:\n"
              << "  -s <socket>    override socket path\n";
}

static std::string recv_line(int fd, int timeout_ms = 5000) {
    char        buf[4096];
    std::string acc;
    while (true) {
        pollfd pfd{fd, POLLIN, 0};
        int    r = poll(&pfd, 1, timeout_ms);
        if (r <= 0)
            break; // timeout or error
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        acc.append(buf, n);
        if (acc.find('\n') != std::string::npos)
            break;
    }
    auto nl = acc.find('\n');
    return nl == std::string::npos ? acc : acc.substr(0, nl);
}

int main(int argc, char** argv) {
    g_log_level = LogLevel::Warn; // quiet by default for CLI tool

    std::string socket_path = proto::SOCKET_PATH;
    std::string cmd;
    std::string profile;

    int i = 1;
    for (; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-s" && i + 1 < argc)
            socket_path = argv[++i];
        else
            break;
    }

    if (i >= argc) {
        usage(argv[0]);
        return 1;
    }
    cmd = argv[i++];

    if (cmd == "status") {
    } else if (cmd == "list-profiles") {
    } else if (cmd == "set-profile") {
        if (i >= argc) {
            std::cerr << "set-profile requires <name>\n";
            return 1;
        }
        profile = argv[i++];
    } else if (cmd == "reload-config") {
    } else if (cmd == "list-inhibitors") {
    } else if (cmd == "lock") {
    } else {
        std::cerr << "unknown command: " << cmd << "\n";
        usage(argv[0]);
        return 1;
    }

    int fd = sock::connect_unix(socket_path);
    if (fd < 0) {
        std::cerr << "cannot connect to draind at " << socket_path << ": " << strerror(errno)
                  << "\n";
        return 2;
    }

    std::string req;
    if (cmd == "status")
        req = proto::encode_ctl("status");
    else if (cmd == "list-profiles")
        req = proto::encode_ctl("list_profiles");
    else if (cmd == "set-profile")
        req = proto::encode_ctl("set_profile", profile);
    else if (cmd == "reload-config")
        req = proto::encode_ctl("reload_config");
    else if (cmd == "list-inhibitors")
        req = proto::encode_ctl("list_inhibitors");
    else if (cmd == "lock")
        req = proto::encode_ctl("lock");

    sock::write_line(fd, req);

    std::string line = recv_line(fd);
    close(fd);

    if (line.empty()) {
        std::cerr << "no response from daemon\n";
        return 3;
    }

    json::Value resp;
    try {
        resp = json::parse(line);
    } catch (...) {
        std::cerr << "malformed response: " << line << "\n";
        return 3;
    }

    bool ok = resp.flag("ok");
    if (!ok) {
        std::cerr << "error: " << resp.str("error", "unknown error") << "\n";
        return 4;
    }

    if (cmd == "status") {
        std::cout << "profile:        " << resp.str("active_profile") << "\n";
        std::cout << "dimmed:         " << (resp.flag("dimmed") ? "yes" : "no") << "\n";
        std::cout << "active_session: " << resp.str("active_session") << "\n";
    } else if (cmd == "list-inhibitors") {
        const auto& inhibitors = resp["inhibitors"].get_array();
        if (inhibitors.empty()) {
            std::cout << "none\n";
        } else {
            for (const auto& v : inhibitors)
                std::cout << v.get_string() << "\n";
        }
    } else if (cmd == "list-profiles") {
        for (const auto& p : resp["profiles"].get_array())
            std::cout << p.get_string() << "\n";
    } else {
        std::cout << "ok\n";
    }

    return 0;
}
