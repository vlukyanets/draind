#pragma once
#include <functional>

namespace draind {

// Abstract interface for idle detection.
// Implementations: WaylandIdleMonitor (Wayland ext_idle_notify_v1)
//                  InputIdleMonitor   (/dev/input/* epoll fallback)
class IIdleMonitor {
  public:
    using Callback = std::function<void()>;

    virtual ~IIdleMonitor() = default;

    // Returns false if this backend is unavailable (never an error; caller falls back).
    virtual bool init(int dim_ms, int sleep_ms) = 0;

    // Update timeouts (e.g. after config reload).
    virtual void set_timeouts(int dim_ms, int sleep_ms) = 0;

    // Drain pending events. Called when fd() is readable.
    virtual void poll() = 0;

    // epoll-watchable fd, or -1 if this monitor has no fd (should not happen).
    virtual int fd() const = 0;

    // Callbacks set by Agent.
    void on_dim(Callback cb)    { m_on_dim    = std::move(cb); }
    void on_sleep(Callback cb)  { m_on_sleep  = std::move(cb); }
    void on_active(Callback cb) { m_on_active = std::move(cb); }

  protected:
    Callback m_on_dim;
    Callback m_on_sleep;
    Callback m_on_active;
};

} // namespace draind
