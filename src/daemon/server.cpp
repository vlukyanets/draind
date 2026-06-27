#include "server.hpp"
#include "../shared/logger.hpp"
#include "../shared/socket.hpp"

#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace draind {

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
    if (m_on_connect) m_on_connect(fd);
    return fd;
}

void Server::handle_client(int fd) {
    auto it = m_conns.find(fd);
    if (it == m_conns.end()) return;

    std::vector<std::string> lines;
    bool ok = it->second.buf.feed(fd, lines);

    for (auto& line : lines) {
        if (line.empty()) continue;
        if (m_on_line) m_on_line(fd, line);
    }

    if (!ok) {
        LOG_DEBUG << "server: client disconnected fd=" << fd;
        if (m_on_disconnect) m_on_disconnect(fd);
        close_client(fd);
    }
}

bool Server::send(int fd, const std::string& line) {
    return sock::write_line(fd, line);
}

void Server::close_client(int fd) {
    m_conns.erase(fd);
    close(fd);
}

} // namespace draind
