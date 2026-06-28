#pragma once
#include "idle_monitor.hpp"
#include <cstdint>

namespace draind::agent {

// Fallback idle monitor: watches /dev/input/event* via epoll.
// Fires on_dim after dim_ms of inactivity, on_sleep after sleep_ms,
// and on_active on the first event after a dim or sleep trigger.
class InputIdleMonitor : public IIdleMonitor {
  public:
    ~InputIdleMonitor() override;

    bool init(int dim_ms, int sleep_ms) override;
    void set_timeouts(int dim_ms, int sleep_ms) override;
    void poll() override;
    int  fd() const override { return m_epoll_fd; }

    // Returns timeout (ms) to use in epoll_wait, or -1 if no sleep configured.
    int next_timeout_ms() const;

    // Call when epoll_wait times out (no input).
    void on_timeout();

  private:
    bool open_input_devices();
    void reset_idle_timer();

    int      m_epoll_fd      = -1;
    int      m_timer_fd      = -1; // timerfd for idle detection
    int      m_dim_ms        = 0;
    int      m_sleep_ms      = 0;
    bool     m_dimmed        = false;
    bool     m_sleeping      = false;
    uint64_t m_last_event_ms = 0; // monotonic ms of last input
};

} // namespace draind::agent
