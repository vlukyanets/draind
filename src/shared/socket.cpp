#include "socket.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace draind::sock {

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void mkdir_p(const std::string& path) {
    // Create all directories up to the last '/'
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            std::string sub = path.substr(0, i);
            mkdir(sub.c_str(), 0755);
        }
    }
}

int listen_unix(const std::string& path) {
    // Ensure parent directory exists
    auto slash = path.rfind('/');
    if (slash != std::string::npos)
        mkdir_p(path.substr(0, slash + 1));

    unlink(path.c_str());

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    chmod(path.c_str(), 0666);

    if (listen(fd, 16) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int connect_unix(const std::string& path) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    set_nonblocking(fd);
    return fd;
}

bool write_line(int fd, std::string_view msg) {
    // Write msg + '\n' in one call where possible
    std::string line(msg);
    line += '\n';
    const char* p   = line.data();
    ssize_t     rem = (ssize_t)line.size();
    while (rem > 0) {
        ssize_t n = write(fd, p, rem);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        p += n;
        rem -= n;
    }
    return true;
}

bool LineBuffer::feed(int fd, std::vector<std::string>& lines) {
    char buf[4096];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false; // EOF
        m_buf.append(buf, n);
    }

    size_t pos = 0;
    size_t nl;
    while ((nl = m_buf.find('\n', pos)) != std::string::npos) {
        lines.push_back(m_buf.substr(pos, nl - pos));
        pos = nl + 1;
    }
    m_buf.erase(0, pos);
    return true;
}

} // namespace draind::sock
