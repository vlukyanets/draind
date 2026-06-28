#include "daemon.hpp"
#include "config_manager.hpp"
#include "hw_event_monitor.hpp"
#include "power_manager.hpp"
#include "server.hpp"
#include "session_tracker.hpp"

#include "../shared/json.hpp"
#include "../shared/logger.hpp"
#include "../shared/protocol.hpp"

#include <csignal>
#include <set>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <unordered_map>

namespace draind::daemon {

// ── Per-agent state ───────────────────────────────────────────────────────────

struct AgentState {
    std::string           session_id;
    uint32_t              uid = 0;
    std::set<std::string> inhibits;
};

// ── Impl ──────────────────────────────────────────────────────────────────────

struct Daemon::Impl {
    ConfigManager  config;
    PowerManager   power;
    SessionTracker sessions;
    Server         server;
    HwEventMonitor hw_events;
    int            epoll_fd = -1;

    std::unordered_map<int, AgentState> agents; // fd → state

    explicit Impl(const std::string& cfg_path) : config(cfg_path) {}
};

volatile sig_atomic_t Daemon::s_quit = 0;
void                  Daemon::signal_handler(int) { s_quit = 1; }

// ── Daemon ────────────────────────────────────────────────────────────────────

Daemon::Daemon(DaemonOptions opts) : m_opts(std::move(opts)) { g_log_level = m_opts.log_level; }

Daemon::~Daemon() {
    if (m_impl && m_impl->epoll_fd >= 0)
        close(m_impl->epoll_fd);
    delete m_impl;
}

int Daemon::run() {
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGHUP, SIG_IGN);
    try {
        setup();
        loop();
    } catch (const std::exception& e) {
        LOG_ERROR << "fatal: " << e.what();
        return 1;
    }
    return 0;
}

static void epoll_add(int epfd, int fd) {
    if (fd < 0)
        return;
    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
        LOG_WARN << "epoll_ctl ADD fd=" << fd << ": " << strerror(errno);
}

static void epoll_del(int epfd, int fd) {
    if (fd >= 0)
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}

void Daemon::setup() {
    m_impl   = new Impl(m_opts.config_path);
    auto& im = *m_impl;

    im.config.on_change([this](const Config&) {
        const Profile* p = m_impl->config.active_profile();
        if (p)
            m_impl->power.apply(*p);
        broadcast_config();
    });

    if (!im.sessions.init())
        LOG_WARN << "daemon: session tracking unavailable";

    if (!im.server.init(proto::SOCKET_PATH))
        throw std::runtime_error("cannot create server socket");

    im.server.on_connect([](int fd) { LOG_DEBUG << "daemon: new connection fd=" << fd; });
    im.server.on_line([this](int fd, const std::string& line) { on_line(fd, line); });
    im.server.on_disconnect([this](int fd) { on_disconnect(fd); });

    const Profile* p = im.config.active_profile();
    if (p)
        im.power.apply(*p);

    im.hw_events.on_event([this](HwEvent e) {
        const Profile* p = m_impl->config.active_profile();
        if (!p)
            return;
        std::string action;
        if (e == HwEvent::LidClose)
            action = p->lid_close_action;
        else if (e == HwEvent::PowerButton)
            action = p->power_button_action;
        else if (e == HwEvent::SleepButton)
            action = p->sleep_button_action;
        if (!action.empty() && action != "none") {
            if (action == "suspend" || action == "hibernate" || action == "hybrid-sleep")
                do_suspend(action);
            else
                run_hw_action(action);
        }
    });
    if (!im.hw_events.init())
        LOG_WARN << "daemon: hardware event monitoring unavailable";

    im.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    epoll_add(im.epoll_fd, im.server.listen_fd());
    epoll_add(im.epoll_fd, im.sessions.bus_fd());
    epoll_add(im.epoll_fd, im.hw_events.fd());

    LOG_INFO << "draind started";
}

void Daemon::loop() {
    auto&       im = *m_impl;
    epoll_event events[16];

    while (!s_quit) {
        int n = epoll_wait(im.epoll_fd, events, 16, -1);
        if (n < 0 && errno != EINTR) {
            LOG_WARN << "epoll_wait: " << strerror(errno);
            continue;
        }

        im.sessions.poll();

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (im.server.is_listen_fd(fd)) {
                int client = im.server.accept_client();
                if (client >= 0)
                    epoll_add(im.epoll_fd, client);
            } else if (im.server.has_fd(fd)) {
                im.server.handle_client(fd);
            } else if (fd == im.hw_events.fd()) {
                im.hw_events.poll();
            }
            // sessions.bus_fd() is polled unconditionally above
        }
    }

    LOG_INFO << "draind stopping";
}

// ── Message dispatch ──────────────────────────────────────────────────────────

void Daemon::on_line(int fd, const std::string& line) {
    json::Value msg;
    try {
        msg = proto::decode(line);
    } catch (...) {
        LOG_WARN << "daemon: malformed message from fd=" << fd << ": " << line;
        m_impl->server.close_client(fd);
        epoll_del(m_impl->epoll_fd, fd);
        return;
    }

    std::string type = proto::msg_type(msg);

    if (type == proto::T_HELLO) {
        on_hello(fd, msg);
        return;
    }
    if (type == proto::T_CTL) {
        on_ctl(fd, msg);
        return;
    }

    // All remaining types require a registered agent
    if (!m_impl->agents.count(fd)) {
        LOG_WARN << "daemon: unexpected message before hello fd=" << fd;
        return;
    }

    if (type == proto::T_IDLE_DIM)
        on_idle_dim(fd);
    else if (type == proto::T_IDLE_SLEEP)
        on_idle_sleep(fd);
    else if (type == proto::T_ACTIVE)
        on_active(fd);
    else if (type == proto::T_INHIBIT)
        on_inhibit(fd, msg);
    else if (type == proto::T_UNINHIBIT)
        on_uninhibit(fd, msg);
    else
        LOG_WARN << "daemon: unknown message type '" << type << "' fd=" << fd;
}

void Daemon::on_disconnect(int fd) {
    auto it = m_impl->agents.find(fd);
    if (it != m_impl->agents.end()) {
        LOG_INFO << "daemon: agent disconnected session=" << it->second.session_id;
        // If this agent was holding inhibits, those are now gone.
        m_impl->agents.erase(it);
    }
    epoll_del(m_impl->epoll_fd, fd);
}

// ── Agent handlers ────────────────────────────────────────────────────────────

void Daemon::on_hello(int fd, const json::Value& msg) {
    AgentState s;
    s.session_id       = msg.str("session_id");
    s.uid              = (uint32_t)msg.num("uid");
    m_impl->agents[fd] = std::move(s);
    LOG_INFO << "daemon: agent registered session=" << m_impl->agents[fd].session_id
             << " uid=" << m_impl->agents[fd].uid << " fd=" << fd;
    send_config(fd);
}

void Daemon::on_idle_dim(int fd) {
    if (!is_active_agent(fd)) {
        LOG_DEBUG << "daemon: idle_dim from inactive session fd=" << fd << " — ignored";
        return;
    }
    if (has_any_inhibit()) {
        LOG_DEBUG << "daemon: idle_dim suppressed by inhibitor";
        return;
    }
    if (m_dimmed)
        return;
    const Profile* p = m_impl->config.active_profile();
    if (!p)
        return;
    m_impl->power.dim(*p);
    m_dimmed = true;
}

void Daemon::on_idle_sleep(int fd) {
    if (!is_active_agent(fd)) {
        LOG_DEBUG << "daemon: idle_sleep from inactive session fd=" << fd << " — ignored";
        return;
    }
    if (has_any_inhibit()) {
        LOG_DEBUG << "daemon: idle_sleep suppressed by inhibitor";
        return;
    }
    const Profile* p = m_impl->config.active_profile();
    if (!p || p->sleep_timeout <= 0)
        return;
    do_suspend("suspend");
}

void Daemon::on_active(int fd) {
    if (!is_active_agent(fd))
        return;
    if (!m_dimmed)
        return;
    const Profile* p = m_impl->config.active_profile();
    if (!p)
        return;
    m_impl->power.undim(*p);
    m_dimmed = false;
}

void Daemon::on_inhibit(int fd, const json::Value& msg) {
    auto it = m_impl->agents.find(fd);
    if (it == m_impl->agents.end())
        return;
    std::string reason = msg.str("reason");
    it->second.inhibits.insert(reason);
    LOG_INFO << "daemon: inhibit from session=" << it->second.session_id << " reason=" << reason;
    m_impl->server.send(fd, proto::encode_ack());
}

void Daemon::on_uninhibit(int fd, const json::Value& msg) {
    auto it = m_impl->agents.find(fd);
    if (it == m_impl->agents.end())
        return;
    std::string reason = msg.str("reason");
    it->second.inhibits.erase(reason);
    LOG_INFO << "daemon: uninhibit from session=" << it->second.session_id << " reason=" << reason;
    m_impl->server.send(fd, proto::encode_ack());
}

// ── Ctl handler ───────────────────────────────────────────────────────────────

void Daemon::on_ctl(int fd, const json::Value& msg) {
    std::string cmd = msg.str("cmd");
    LOG_DEBUG << "daemon: ctl cmd=" << cmd << " fd=" << fd;

    if (cmd == "status") {
        json::Value r;
        r["ok"]             = true;
        r["active_profile"] = m_impl->config.active_profile_name();
        r["dimmed"]         = m_dimmed;
        r["active_session"] = m_impl->sessions.active_session_id();
        m_impl->server.send(fd, proto::encode_ctl_reply(std::move(r)));

    } else if (cmd == "set_profile") {
        std::string name = msg.str("profile");
        bool        ok   = m_impl->config.set_active(name);
        json::Value r;
        r["ok"] = ok;
        if (!ok)
            r["error"] = "profile not found: " + name;
        m_impl->server.send(fd, proto::encode_ctl_reply(std::move(r)));

    } else if (cmd == "list_profiles") {
        json::Value r;
        r["ok"] = true;
        json::Array arr;
        for (const auto& p : m_impl->config.config().profiles)
            arr.push_back(json::Value(p.name));
        r["profiles"] = json::Value(std::move(arr));
        m_impl->server.send(fd, proto::encode_ctl_reply(std::move(r)));

    } else if (cmd == "lock") {
        send_lock_to_active_agent();
        json::Value r;
        r["ok"] = true;
        m_impl->server.send(fd, proto::encode_ctl_reply(std::move(r)));

    } else if (cmd == "list_inhibitors") {
        json::Value r;
        r["ok"] = true;
        json::Array inhibitors;
        for (const auto& [afd, agent] : m_impl->agents)
            for (const auto& reason : agent.inhibits)
                inhibitors.push_back(json::Value(
                    agent.session_id.empty() ? reason : agent.session_id + ": " + reason));
        r["inhibitors"] = json::Value(std::move(inhibitors));
        m_impl->server.send(fd, proto::encode_ctl_reply(std::move(r)));

    } else if (cmd == "reload_config") {
        try {
            m_impl->config.reload();
            json::Value r;
            r["ok"] = true;
            m_impl->server.send(fd, proto::encode_ctl_reply(std::move(r)));
        } catch (const std::exception& e) {
            json::Value r;
            r["ok"]    = false;
            r["error"] = e.what();
            m_impl->server.send(fd, proto::encode_ctl_reply(std::move(r)));
        }

    } else {
        json::Value r;
        r["ok"]    = false;
        r["error"] = "unknown command: " + cmd;
        m_impl->server.send(fd, proto::encode_ctl_reply(std::move(r)));
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool Daemon::is_active_agent(int fd) const {
    auto it = m_impl->agents.find(fd);
    if (it == m_impl->agents.end())
        return false;
    return m_impl->sessions.is_active(it->second.session_id);
}

bool Daemon::has_any_inhibit() const {
    for (const auto& [fd, agent] : m_impl->agents)
        if (!agent.inhibits.empty())
            return true;
    return false;
}

void Daemon::send_config(int fd) {
    const Profile* p     = m_impl->config.active_profile();
    int            dim   = p ? p->dim_timeout : 0;
    int            sleep = p ? p->sleep_timeout : 0;
    m_impl->server.send(fd, proto::encode_config(dim, sleep));
    LOG_DEBUG << "daemon: sent config to fd=" << fd << " dim=" << dim << "s sleep=" << sleep << "s";
}

void Daemon::broadcast_config() {
    for (const auto& [fd, _] : m_impl->agents)
        send_config(fd);
}

void Daemon::send_lock_to_active_agent() {
    for (const auto& [fd, agent] : m_impl->agents) {
        if (m_impl->sessions.is_active(agent.session_id)) {
            LOG_DEBUG << "daemon: sending lock to session=" << agent.session_id;
            m_impl->server.send(fd, proto::encode_lock());
            return;
        }
    }
    LOG_WARN << "daemon: no agent matched active session — lock not sent"
             << " (active=" << m_impl->sessions.active_session_id() << ","
             << " agents=" << m_impl->agents.size() << ")";
}

void Daemon::do_suspend(const std::string& action) {
    LOG_INFO << "daemon: pre-suspend sequence";
    send_lock_to_active_agent();
    m_impl->server.broadcast_and_wait_acks(proto::encode_pre_sleep(), 5000);
    run_hw_action(action);
}

} // namespace draind::daemon
