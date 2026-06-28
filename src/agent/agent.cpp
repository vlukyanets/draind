#include "agent.hpp"
#include "idle_monitor.hpp"
#include "input_idle_monitor.hpp"
#include "mpris_monitor.hpp"

#ifdef HAVE_WAYLAND
#include "wayland_idle_monitor.hpp"
#endif

#include "../shared/json.hpp"
#include "../shared/logger.hpp"
#include "../shared/protocol.hpp"
#include "../shared/socket.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace draind::agent {

static volatile sig_atomic_t s_quit = 0;
static void                  on_signal(int) { s_quit = 1; }

struct Agent::Impl {
    int                           daemon_fd = -1;
    int                           epoll_fd  = -1;
    sock::LineBuffer              daemon_buf;
    std::unique_ptr<IIdleMonitor> idle;
    MprisMonitor                  mpris;
};

Agent::Agent(AgentOptions opts) : m_opts(std::move(opts)) {
    g_log_level = m_opts.log_level;
    if (m_opts.socket_path.empty())
        m_opts.socket_path = proto::SOCKET_PATH;
}

Agent::~Agent() {
    if (m_impl) {
        if (m_impl->epoll_fd >= 0)
            close(m_impl->epoll_fd);
        if (m_impl->daemon_fd >= 0)
            close(m_impl->daemon_fd);
    }
    delete m_impl;
}

int Agent::run() {
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGHUP, SIG_IGN);

    m_impl           = new Impl();
    m_impl->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    connect_to_daemon();

    if (!m_impl->mpris.init())
        LOG_WARN << "agent: MPRIS monitor unavailable";
    else {
        m_impl->mpris.on_inhibit([this](const std::string& r) { send(proto::encode_inhibit(r)); });
        m_impl->mpris.on_uninhibit(
            [this](const std::string& r) { send(proto::encode_uninhibit(r)); });
        int mfd = m_impl->mpris.bus_fd();
        if (mfd >= 0) {
            epoll_event ev{};
            ev.events  = EPOLLIN;
            ev.data.fd = mfd;
            epoll_ctl(m_impl->epoll_fd, EPOLL_CTL_ADD, mfd, &ev);
        }
    }

    loop();
    return 0;
}

void Agent::connect_to_daemon() {
    while (!s_quit) {
        m_impl->daemon_fd = sock::connect_unix(m_opts.socket_path);
        if (m_impl->daemon_fd >= 0)
            break;
        LOG_WARN << "agent: cannot connect to daemon — retrying in 2s";
        sleep(2);
    }
    if (s_quit)
        return;

    LOG_INFO << "agent: connected to daemon";

    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = m_impl->daemon_fd;
    epoll_ctl(m_impl->epoll_fd, EPOLL_CTL_ADD, m_impl->daemon_fd, &ev);

    send(proto::encode_hello(m_opts.session_id, m_opts.uid));
}

void Agent::setup_idle_monitor(int dim_ms, int sleep_ms) {
    // Remove old idle monitor fd from epoll if any
    if (m_impl->idle) {
        int old_fd = m_impl->idle->fd();
        if (old_fd >= 0)
            epoll_ctl(m_impl->epoll_fd, EPOLL_CTL_DEL, old_fd, nullptr);
        m_impl->idle.reset();
    }

    std::unique_ptr<IIdleMonitor> mon;

#ifdef HAVE_WAYLAND
    auto wl = std::make_unique<WaylandIdleMonitor>();
    if (wl->init(dim_ms, sleep_ms)) {
        mon = std::move(wl);
        LOG_INFO << "agent: using Wayland idle monitor";
    }
#endif

    if (!mon) {
        auto inp = std::make_unique<InputIdleMonitor>();
        if (!inp->init(dim_ms, sleep_ms)) {
            LOG_ERROR << "agent: InputIdleMonitor init failed — no idle detection";
            return;
        }
        mon = std::move(inp);
        LOG_INFO << "agent: using /dev/input idle monitor";
    }

    mon->on_dim([this]() { send(proto::encode_idle_dim()); });
    mon->on_sleep([this]() { send(proto::encode_idle_sleep()); });
    mon->on_active([this]() { send(proto::encode_active()); });

    int ifd = mon->fd();
    if (ifd >= 0) {
        epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.fd = ifd;
        epoll_ctl(m_impl->epoll_fd, EPOLL_CTL_ADD, ifd, &ev);
    }

    m_impl->idle = std::move(mon);
}

void Agent::loop() {
    epoll_event events[16];
    while (!s_quit) {
        int n = epoll_wait(m_impl->epoll_fd, events, 16, -1);
        if (n < 0 && errno != EINTR)
            continue;

        m_impl->mpris.poll();

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == m_impl->daemon_fd) {
                std::vector<std::string> lines;
                if (!m_impl->daemon_buf.feed(fd, lines)) {
                    on_daemon_disconnect();
                    return;
                }
                for (auto& l : lines) {
                    if (!l.empty())
                        on_daemon_line(l);
                }
            } else if (m_impl->idle && fd == m_impl->idle->fd()) {
                m_impl->idle->poll();
            }
            // mpris polled unconditionally above
        }
    }
}

void Agent::on_daemon_line(const std::string& line) {
    json::Value msg;
    try {
        msg = proto::decode(line);
    } catch (...) {
        LOG_WARN << "agent: malformed message: " << line;
        return;
    }

    std::string type = proto::msg_type(msg);

    if (type == proto::T_CONFIG) {
        int dim   = (int)msg.num("dim_timeout", 0) * 1000;
        int sleep = (int)msg.num("sleep_timeout", 0) * 1000;
        LOG_INFO << "agent: received config dim=" << dim << "ms sleep=" << sleep << "ms";
        if (m_impl->idle)
            m_impl->idle->set_timeouts(dim, sleep);
        else
            setup_idle_monitor(dim, sleep);
    } else if (type == proto::T_LOCK) {
        run_lock_cmd();
    } else if (type == proto::T_PRE_SLEEP) {
        run_before_sleep_cmd();
        send(proto::encode_ack());
    } else if (type == proto::T_ACK) {
        // nothing
    } else {
        LOG_DEBUG << "agent: unhandled message type=" << type;
    }
}

void Agent::run_lock_cmd() {
    if (m_opts.lock_cmd.empty()) {
        LOG_DEBUG << "agent: lock requested but no lock_cmd configured";
        return;
    }
    LOG_INFO << "agent: running lock_cmd";
    pid_t pid = fork();
    if (pid < 0) {
        LOG_WARN << "agent: fork failed for lock_cmd: " << strerror(errno);
        return;
    }
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", m_opts.lock_cmd.c_str(), nullptr);
        _exit(127);
    }
}

void Agent::run_before_sleep_cmd() {
    if (m_opts.before_sleep_cmd.empty())
        return;
    LOG_INFO << "agent: running before_sleep_cmd";
    pid_t pid = fork();
    if (pid < 0) {
        LOG_WARN << "agent: fork failed for before_sleep_cmd: " << strerror(errno);
        return;
    }
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", m_opts.before_sleep_cmd.c_str(), nullptr);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
        LOG_WARN << "agent: before_sleep_cmd killed by signal " << WTERMSIG(status);
    else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        LOG_WARN << "agent: before_sleep_cmd exited " << WEXITSTATUS(status);
}

void Agent::on_daemon_disconnect() {
    LOG_WARN << "agent: daemon disconnected — reconnecting";
    epoll_ctl(m_impl->epoll_fd, EPOLL_CTL_DEL, m_impl->daemon_fd, nullptr);
    close(m_impl->daemon_fd);
    m_impl->daemon_fd  = -1;
    m_impl->daemon_buf = {};
    connect_to_daemon();
}

void Agent::send(const std::string& msg) {
    if (m_impl->daemon_fd < 0)
        return;
    if (!sock::write_line(m_impl->daemon_fd, msg))
        LOG_WARN << "agent: write to daemon failed";
}

} // namespace draind::agent
