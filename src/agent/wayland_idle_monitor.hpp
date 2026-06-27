#pragma once
#ifdef HAVE_WAYLAND

#include "idle_monitor.hpp"
#include <wayland-client.h>
// Generated from ext-idle-notify-v1.xml by wayland-scanner:
#include "ext-idle-notify-v1-client-protocol.h"

namespace draind {

class WaylandIdleMonitor : public IIdleMonitor {
  public:
    ~WaylandIdleMonitor() override;

    bool init(int dim_ms, int sleep_ms) override;
    void set_timeouts(int dim_ms, int sleep_ms) override;
    void poll() override;
    int  fd() const override;

    // Wayland listener callbacks (called by generated code)
    void on_notification_idled(ext_idle_notification_v1* notif);
    void on_notification_resumed(ext_idle_notification_v1* notif);

  private:
    void destroy_notifications();
    bool create_notifications(int dim_ms, int sleep_ms);

    wl_display*                m_display    = nullptr;
    wl_registry*               m_registry   = nullptr;
    ext_idle_notifier_v1*      m_notifier   = nullptr;
    wl_seat*                   m_seat       = nullptr;
    ext_idle_notification_v1*  m_notif_dim  = nullptr;
    ext_idle_notification_v1*  m_notif_sleep = nullptr;
};

} // namespace draind

#endif // HAVE_WAYLAND
