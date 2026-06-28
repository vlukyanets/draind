#ifdef HAVE_WAYLAND

#include "wayland_idle_monitor.hpp"
#include "../shared/logger.hpp"

#include <cstring>

namespace draind::agent {

// ── Notification listeners ────────────────────────────────────────────────────

static void notif_idled(void* data, ext_idle_notification_v1* notif) {
    static_cast<WaylandIdleMonitor*>(data)->on_notification_idled(notif);
}
static void notif_resumed(void* data, ext_idle_notification_v1* notif) {
    static_cast<WaylandIdleMonitor*>(data)->on_notification_resumed(notif);
}

static constexpr ext_idle_notification_v1_listener k_notif_listener = {
    .idled   = notif_idled,
    .resumed = notif_resumed,
};

// ── WaylandIdleMonitor ────────────────────────────────────────────────────────

WaylandIdleMonitor::~WaylandIdleMonitor() {
    destroy_notifications();
    if (m_notifier)
        ext_idle_notifier_v1_destroy(m_notifier);
    if (m_seat)
        wl_seat_destroy(m_seat);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_display)
        wl_display_disconnect(m_display);
}

bool WaylandIdleMonitor::init(int dim_ms, int sleep_ms) {
    const char* disp = getenv("WAYLAND_DISPLAY");
    if (!disp) {
        LOG_INFO << "wayland_idle: WAYLAND_DISPLAY not set — skipping";
        return false;
    }

    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        LOG_INFO << "wayland_idle: wl_display_connect failed — skipping";
        return false;
    }

    // Use a simple synchronous registry enumeration.
    // We store the bound globals through the listener using local variables
    // captured via a small context struct.

    struct Ctx {
        ext_idle_notifier_v1* notifier = nullptr;
        wl_seat*              seat     = nullptr;
    } ctx;

    wl_registry* reg = wl_display_get_registry(m_display);

    static const wl_registry_listener kRegListener = {
        [](void* d, wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
            auto* c = static_cast<Ctx*>(d);
            if (strcmp(iface, ext_idle_notifier_v1_interface.name) == 0)
                c->notifier = static_cast<ext_idle_notifier_v1*>(
                    wl_registry_bind(r, name, &ext_idle_notifier_v1_interface, std::min(ver, 1u)));
            else if (strcmp(iface, wl_seat_interface.name) == 0)
                c->seat = static_cast<wl_seat*>(
                    wl_registry_bind(r, name, &wl_seat_interface, std::min(ver, 1u)));
        },
        [](void*, wl_registry*, uint32_t) {},
    };

    wl_registry_add_listener(reg, &kRegListener, &ctx);
    wl_display_roundtrip(m_display);

    m_registry = reg;
    m_notifier = ctx.notifier;
    m_seat     = ctx.seat;

    if (!m_notifier) {
        LOG_INFO << "wayland_idle: compositor lacks ext_idle_notifier_v1 — skipping";
        return false;
    }
    if (!m_seat) {
        LOG_WARN << "wayland_idle: no wl_seat found";
        return false;
    }

    if (!create_notifications(dim_ms, sleep_ms))
        return false;

    LOG_INFO << "wayland_idle: initialized dim=" << dim_ms << "ms sleep=" << sleep_ms << "ms";
    return true;
}

void WaylandIdleMonitor::destroy_notifications() {
    if (m_notif_sleep) {
        ext_idle_notification_v1_destroy(m_notif_sleep);
        m_notif_sleep = nullptr;
    }
    if (m_notif_dim) {
        ext_idle_notification_v1_destroy(m_notif_dim);
        m_notif_dim = nullptr;
    }
}

bool WaylandIdleMonitor::create_notifications(int dim_ms, int sleep_ms) {
    destroy_notifications();

    if (dim_ms > 0) {
        m_notif_dim =
            ext_idle_notifier_v1_get_idle_notification(m_notifier, (uint32_t)dim_ms, m_seat);
        if (!m_notif_dim) {
            LOG_WARN << "wayland_idle: failed to create dim notification";
            return false;
        }
        ext_idle_notification_v1_add_listener(m_notif_dim, &k_notif_listener, this);
    }

    if (sleep_ms > 0) {
        m_notif_sleep =
            ext_idle_notifier_v1_get_idle_notification(m_notifier, (uint32_t)sleep_ms, m_seat);
        if (!m_notif_sleep) {
            LOG_WARN << "wayland_idle: failed to create sleep notification";
            return false;
        }
        ext_idle_notification_v1_add_listener(m_notif_sleep, &k_notif_listener, this);
    }

    wl_display_flush(m_display);
    return true;
}

void WaylandIdleMonitor::set_timeouts(int dim_ms, int sleep_ms) {
    create_notifications(dim_ms, sleep_ms);
}

void WaylandIdleMonitor::poll() { wl_display_dispatch(m_display); }

int WaylandIdleMonitor::fd() const { return m_display ? wl_display_get_fd(m_display) : -1; }

void WaylandIdleMonitor::on_notification_idled(ext_idle_notification_v1* notif) {
    if (notif == m_notif_dim && m_on_dim) {
        LOG_DEBUG << "wayland_idle: dim threshold idled";
        m_on_dim();
    } else if (notif == m_notif_sleep && m_on_sleep) {
        LOG_DEBUG << "wayland_idle: sleep threshold idled";
        m_on_sleep();
    }
}

void WaylandIdleMonitor::on_notification_resumed(ext_idle_notification_v1* notif) {
    (void)notif;
    if (m_on_active) {
        LOG_DEBUG << "wayland_idle: resumed";
        m_on_active();
    }
}

} // namespace draind::agent

#endif // HAVE_WAYLAND
