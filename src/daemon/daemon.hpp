#pragma once
#include "../shared/json.hpp"
#include "../shared/logger.hpp"
#include <csignal>
#include <string>

namespace draind {

struct DaemonOptions {
    std::string config_path = "/etc/draind/draind.json";
    bool        foreground  = false;
    LogLevel    log_level   = LogLevel::Info;
};

class Daemon {
  public:
    explicit Daemon(DaemonOptions opts);
    ~Daemon();

    Daemon(const Daemon&)            = delete;
    Daemon& operator=(const Daemon&) = delete;

    int run(); // returns exit code

  private:
    void setup();
    void loop();

    // Message dispatch
    void on_line(int fd, const std::string& line);
    void on_disconnect(int fd);

    // Agent message handlers
    void on_hello(int fd, const json::Value& msg);
    void on_idle_dim(int fd);
    void on_idle_sleep(int fd);
    void on_active(int fd);
    void on_inhibit(int fd, const json::Value& msg);
    void on_uninhibit(int fd, const json::Value& msg);

    // Ctl message handler
    void on_ctl(int fd, const json::Value& msg);

    // Policy helpers
    bool is_active_agent(int fd) const;
    bool has_any_inhibit() const;
    void send_config(int fd);
    void broadcast_config();

    static volatile sig_atomic_t s_quit;
    static void signal_handler(int);

    struct Impl;
    Impl*         m_impl = nullptr;
    DaemonOptions m_opts;
    bool          m_dimmed = false;
};

} // namespace draind
