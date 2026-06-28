#pragma once
// Accepts agent and ctl connections on the Unix socket.
// Handles framing (line buffering) and exposes raw lines to the Daemon.

#include "../shared/socket.hpp"
#include <functional>
#include <string>
#include <unordered_map>

namespace draind::daemon {

struct Connection {
    int              fd = -1;
    sock::LineBuffer buf;
    bool             is_agent = false; // set after first message parsed
    bool             is_ctl   = false;
};

class Server {
  public:
    using LineCb       = std::function<void(int fd, const std::string& line)>;
    using ConnectCb    = std::function<void(int fd)>;
    using DisconnectCb = std::function<void(int fd)>;

    bool init(const std::string& path);

    int  listen_fd() const { return m_listen_fd; }
    bool has_fd(int fd) const { return m_conns.count(fd) > 0; }
    bool is_listen_fd(int fd) const { return fd == m_listen_fd; }

    // Call when epoll fires on listen_fd: accepts a new connection.
    // Returns the new client fd (already added internally) or -1.
    int accept_client();

    // Call when epoll fires on a client fd.
    // Reads lines and invokes on_line; invokes on_disconnect on EOF/error.
    void handle_client(int fd);

    bool send(int fd, const std::string& line);
    void close_client(int fd);

    void on_line(LineCb cb) { m_on_line = std::move(cb); }
    void on_connect(ConnectCb cb) { m_on_connect = std::move(cb); }
    void on_disconnect(DisconnectCb cb) { m_on_disconnect = std::move(cb); }

    // Iterate all connected agent fds
    template <typename F> void for_each_agent(F fn) const {
        for (const auto& [fd, c] : m_conns)
            if (c.is_agent)
                fn(fd);
    }

    // Send msg to all agents and wait up to timeout_ms for an ack from each.
    // Acks that arrive through the normal event loop after this call are harmless.
    void broadcast_and_wait_acks(const std::string& msg, int timeout_ms);

  private:
    int                                 m_listen_fd = -1;
    std::unordered_map<int, Connection> m_conns;
    LineCb                              m_on_line;
    ConnectCb                           m_on_connect;
    DisconnectCb                        m_on_disconnect;
};

} // namespace draind::daemon
