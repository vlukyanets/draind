#include "server.hpp"
#include "../shared/logger.hpp"
#include "../shared/socket.hpp"

#include <chrono>
#include <cstring>
#include <poll.h>
#include <set>
#include <sys/socket.h>
#include <unistd.h>

namespace draind::daemon {

bool Server::init(const std::string& path) {
    m_listen_fd = sock::listen_unix(path);
    if (m_listen_fd < 0) {
        LOG_ERROR << "server: listen_unix(" << path << "): " << strerror(errno);
        return false;
    }
    LOG_DEBUG << "server: listening on " << path << " fd=" << m_listen_fd;
    return true;
}

int Server::accept_client() {
    int fd = accept4(m_listen_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            LOG_WARN << "server: accept4: " << strerror(errno);
        return -1;
    }
    m_conns.emplace(fd, Connection{fd, {}, false, false});
    LOG_DEBUG << "server: client connected fd=" << fd;
    if (m_on_connect)
        m_on_connect(fd);
    return fd;
}

void Server::handle_client(int fd) {
    auto it = m_conns.find(fd);
    if (it == m_conns.end())
        return;

    std::vector<std::string> lines;
    bool                     ok = it->second.buf.feed(fd, lines);

    for (auto& line : lines) {
        if (line.empty())
            continue;
        if (m_on_line)
            m_on_line(fd, line);
    }

    if (!ok) {
        LOG_DEBUG << "server: client disconnected fd=" << fd;
        if (m_on_disconnect)
            m_on_disconnect(fd);
        close_client(fd);
    }
}

bool Server::send(int fd, const std::string& line) { return sock::write_line(fd, line); }

void Server::close_client(int fd) {
    m_conns.erase(fd);
    close(fd);
}

void Server::broadcast_and_wait_acks(const std::string& msg, int timeout_ms) {
    std::set<int> pending;
    for (const auto& [fd, c] : m_conns) {
        if (!c.is_agent)
            continue;
        if (sock::write_line(fd, msg))
            pending.insert(fd);
    }
    if (pending.empty())
        return;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (!pending.empty()) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            LOG_WARN << "server: pre_sleep ack timeout — " << pending.size()
                     << " agent(s) did not respond";
            break;
        }
        int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

        std::vector<pollfd> pfds;
        pfds.reserve(pending.size());
        for (int fd : pending)
            pfds.push_back({fd, POLLIN, 0});

        int r = poll(pfds.data(), (nfds_t)pfds.size(), ms);
        if (r <= 0)
            continue;

        for (auto& pfd : pfds) {
            if (!(pfd.revents & POLLIN))
                continue;
            // Read one line directly; the LineBuffer for this fd should be empty
            // because we just sent pre_sleep and the agent hasn't spoken since hello.
            char    buf[256];
            ssize_t n = recv(pfd.fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
            if (n <= 0) {
                pending.erase(pfd.fd);
                continue;
            }
            buf[n] = '\0';
            std::string_view sv(buf, n);
            // Accept the ack if the line contains "ack" (simple check; avoids json dep)
            if (sv.find("\"ack\"") != std::string_view::npos)
                pending.erase(pfd.fd);
        }
    }
}

} // namespace draind::daemon
