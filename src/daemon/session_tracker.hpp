#pragma once
// Watches logind seat0 via sd-bus to track which session is active.

#include <functional>
#include <string>
#include <systemd/sd-bus.h>

namespace draind::daemon {

class SessionTracker {
  public:
    SessionTracker();
    ~SessionTracker();

    SessionTracker(const SessionTracker&)            = delete;
    SessionTracker& operator=(const SessionTracker&) = delete;

    bool init();
    void poll();
    int  bus_fd() const;

    // Returns the session ID of the currently active session on seat0, or "".
    const std::string& active_session_id() const { return m_active_session_id; }
    bool               is_active(const std::string& session_id) const;

    // Called from sd-bus signal handler.
    int handle_seat_properties(sd_bus_message* m);

  private:
    void refresh_active_session();

    sd_bus*     m_bus = nullptr;
    std::string m_seat0_path;
    std::string m_active_session_id;
};

} // namespace draind::daemon
