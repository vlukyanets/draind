#pragma once
#include "../shared/logger.hpp"
#include <string>

namespace draind {

struct AgentOptions {
    std::string socket_path; // default: proto::SOCKET_PATH
    std::string session_id;  // $XDG_SESSION_ID
    uint32_t    uid = 0;
    LogLevel    log_level = LogLevel::Info;
};

class Agent {
  public:
    explicit Agent(AgentOptions opts);
    ~Agent();

    Agent(const Agent&)            = delete;
    Agent& operator=(const Agent&) = delete;

    int run(); // returns exit code

  private:
    void connect_to_daemon();
    void setup_idle_monitor(int dim_ms, int sleep_ms);
    void loop();

    void on_daemon_line(const std::string& line);
    void on_daemon_disconnect();

    void send(const std::string& msg);

    struct Impl;
    Impl*        m_impl = nullptr;
    AgentOptions m_opts;
};

} // namespace draind
