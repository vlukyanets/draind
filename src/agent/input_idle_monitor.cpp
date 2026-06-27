#include "input_idle_monitor.hpp"
#include "../shared/logger.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace draind {

static uint64_t mono_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

InputIdleMonitor::~InputIdleMonitor() {
    if (m_timer_fd >= 0) close(m_timer_fd);
    if (m_epoll_fd >= 0) close(m_epoll_fd);
}

bool InputIdleMonitor::init(int dim_ms, int sleep_ms) {
    m_dim_ms   = dim_ms;
    m_sleep_ms = sleep_ms;

    m_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epoll_fd < 0) {
        LOG_ERROR << "input_idle: epoll_create1: " << strerror(errno);
        return false;
    }

    m_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_timer_fd < 0) {
        LOG_ERROR << "input_idle: timerfd_create: " << strerror(errno);
        return false;
    }

    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = m_timer_fd;
    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_timer_fd, &ev);

    if (!open_input_devices()) {
        LOG_WARN << "input_idle: no /dev/input/event* devices found";
    }

    m_last_event_ms = mono_ms();
    reset_idle_timer();
    LOG_INFO << "input_idle: initialized dim=" << dim_ms << "ms sleep=" << sleep_ms << "ms";
    return true;
}

bool InputIdleMonitor::open_input_devices() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return false;

    int count = 0;
    dirent* de;
    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + de->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.fd = fd;
        epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
        ++count;
        LOG_DEBUG << "input_idle: watching " << path;
    }
    closedir(dir);
    return count > 0;
}

void InputIdleMonitor::set_timeouts(int dim_ms, int sleep_ms) {
    m_dim_ms   = dim_ms;
    m_sleep_ms = sleep_ms;
    reset_idle_timer();
}

void InputIdleMonitor::reset_idle_timer() {
    if (m_timer_fd < 0) return;
    // Fire at the earliest of dim_ms or sleep_ms
    int first_ms = m_dim_ms > 0 ? m_dim_ms
                 : m_sleep_ms > 0 ? m_sleep_ms : 0;
    if (first_ms <= 0) return;

    itimerspec its{};
    its.it_value.tv_sec  = first_ms / 1000;
    its.it_value.tv_nsec = (first_ms % 1000) * 1000000LL;
    timerfd_settime(m_timer_fd, 0, &its, nullptr);
    m_dimmed   = false;
    m_sleeping = false;
}

void InputIdleMonitor::poll() {
    epoll_event events[16];
    int n = epoll_wait(m_epoll_fd, events, 16, 0);
    for (int i = 0; i < n; ++i) {
        int efd = events[i].data.fd;
        if (efd == m_timer_fd) {
            // Drain timerfd
            uint64_t exp;
            (void)read(m_timer_fd, &exp, sizeof(exp));
            on_timeout();
        } else {
            // Drain input device (we don't care about the event content)
            char buf[256];
            while (read(efd, buf, sizeof(buf)) > 0) {}

            bool was_idle = m_dimmed || m_sleeping;
            m_last_event_ms = mono_ms();
            reset_idle_timer();
            if (was_idle && m_on_active) m_on_active();
        }
    }
}

void InputIdleMonitor::on_timeout() {
    uint64_t now     = mono_ms();
    uint64_t elapsed = now - m_last_event_ms;

    if (!m_sleeping && m_sleep_ms > 0 && (int)elapsed >= m_sleep_ms) {
        m_sleeping = true;
        LOG_DEBUG << "input_idle: sleep threshold reached";
        if (m_on_sleep) m_on_sleep();
        return;
    }
    if (!m_dimmed && m_dim_ms > 0 && (int)elapsed >= m_dim_ms) {
        m_dimmed = true;
        LOG_DEBUG << "input_idle: dim threshold reached";
        if (m_on_dim) m_on_dim();

        // Set next timer for sleep if configured
        if (m_sleep_ms > 0 && !m_sleeping) {
            int remain_ms = m_sleep_ms - (int)elapsed;
            if (remain_ms < 1) remain_ms = 1;
            itimerspec its{};
            its.it_value.tv_sec  = remain_ms / 1000;
            its.it_value.tv_nsec = (remain_ms % 1000) * 1000000LL;
            timerfd_settime(m_timer_fd, 0, &its, nullptr);
        }
    }
}

} // namespace draind
