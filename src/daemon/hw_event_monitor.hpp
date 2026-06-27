#pragma once
// Watches /dev/input/event* for lid-switch, power-key, and sleep-key events.
// Runs in the daemon (root), which has access to all input devices.

#include <functional>
#include <string>

namespace draind {

enum class HwEvent { LidClose, LidOpen, PowerButton, SleepButton };

class HwEventMonitor {
  public:
    using Callback = std::function<void(HwEvent)>;

    ~HwEventMonitor();

    // Returns false if no suitable input devices were found (non-fatal).
    bool init();

    void poll();
    int  fd() const { return m_epoll_fd; }

    void on_event(Callback cb) { m_cb = std::move(cb); }

  private:
    int      m_epoll_fd = -1;
    Callback m_cb;
};

// Execute a hardware action string. "none"/"" → no-op.
void run_hw_action(const std::string& action);

} // namespace draind
