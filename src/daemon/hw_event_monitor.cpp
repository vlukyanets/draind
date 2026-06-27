#include "hw_event_monitor.hpp"
#include "../shared/logger.hpp"

#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace draind {

HwEventMonitor::~HwEventMonitor() {
    if (m_epoll_fd >= 0) close(m_epoll_fd);
}

bool HwEventMonitor::init() {
    m_epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epoll_fd < 0) {
        LOG_ERROR << "hw_events: epoll_create1: " << strerror(errno);
        return false;
    }

    DIR* dir = opendir("/dev/input");
    if (!dir) {
        LOG_WARN << "hw_events: cannot open /dev/input: " << strerror(errno);
        return false;
    }

    int count = 0;
    dirent* de;
    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + de->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        // Check if this device reports EV_SW or EV_KEY (power/sleep buttons)
        unsigned long evbits = 0;
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits) >= 0) {
            bool has_sw  = evbits & (1UL << EV_SW);
            bool has_key = evbits & (1UL << EV_KEY);
            if (has_sw || has_key) {
                epoll_event ev{};
                ev.events  = EPOLLIN;
                ev.data.fd = fd;
                epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
                LOG_DEBUG << "hw_events: watching " << path;
                ++count;
                continue;
            }
        }
        close(fd);
    }
    closedir(dir);

    if (count == 0) {
        LOG_WARN << "hw_events: no suitable input devices found";
        return false;
    }
    LOG_INFO << "hw_events: watching " << count << " device(s)";
    return true;
}

void HwEventMonitor::poll() {
    epoll_event events[8];
    int n = epoll_wait(m_epoll_fd, events, 8, 0);
    for (int i = 0; i < n; ++i) {
        int fd = events[i].data.fd;
        input_event ev{};
        while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_SW && ev.code == SW_LID) {
                HwEvent e = ev.value ? HwEvent::LidClose : HwEvent::LidOpen;
                LOG_DEBUG << "hw_events: lid " << (ev.value ? "closed" : "opened");
                if (m_cb) m_cb(e);
            } else if (ev.type == EV_KEY && ev.value == 1 /* key down */) {
                if (ev.code == KEY_POWER) {
                    LOG_DEBUG << "hw_events: power button";
                    if (m_cb) m_cb(HwEvent::PowerButton);
                } else if (ev.code == KEY_SLEEP) {
                    LOG_DEBUG << "hw_events: sleep button";
                    if (m_cb) m_cb(HwEvent::SleepButton);
                }
            }
        }
    }
}

void run_hw_action(const std::string& action) {
    if (action.empty() || action == "none" || action == "ignore") return;

    const char* cmd = nullptr;
    if      (action == "suspend")      cmd = "systemctl suspend";
    else if (action == "hibernate")    cmd = "systemctl hibernate";
    else if (action == "hybrid-sleep") cmd = "systemctl hybrid-sleep";
    else if (action == "poweroff")     cmd = "systemctl poweroff";
    else {
        LOG_WARN << "hw_events: unknown action '" << action << "'";
        return;
    }

    LOG_INFO << "hw_events: running action '" << action << "'";
    int r = system(cmd);
    if (r != 0) LOG_WARN << "hw_events: '" << cmd << "' exited " << r;
}

} // namespace draind
