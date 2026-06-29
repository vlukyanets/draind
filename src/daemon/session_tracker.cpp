#include "session_tracker.hpp"
#include "../shared/logger.hpp"

#include <cstring>
#include <systemd/sd-login.h>

namespace draind::daemon {

static int on_seat_properties(sd_bus_message* m, void* userdata, sd_bus_error*) {
    return static_cast<SessionTracker*>(userdata)->handle_seat_properties(m);
}

SessionTracker::SessionTracker() = default;
SessionTracker::~SessionTracker() {
    if (m_bus)
        sd_bus_unref(m_bus);
}

bool SessionTracker::init() {
    int r = sd_bus_open_system(&m_bus);
    if (r < 0) {
        LOG_ERROR << "session_tracker: sd_bus_open_system: " << strerror(-r);
        return false;
    }

    struct Err {
        sd_bus_error e = SD_BUS_ERROR_NULL;
        ~Err() { sd_bus_error_free(&e); }
    } err;
    struct Msg {
        sd_bus_message* m = nullptr;
        ~Msg() { sd_bus_message_unref(m); }
    } rep;

    r = sd_bus_call_method(m_bus, "org.freedesktop.login1", "/org/freedesktop/login1",
                           "org.freedesktop.login1.Manager", "GetSeat", &err.e, &rep.m, "s",
                           "seat0");
    if (r < 0) {
        LOG_WARN << "session_tracker: GetSeat(seat0): "
                 << (err.e.message ? err.e.message : strerror(-r))
                 << " — session tracking disabled";
        return true; // non-fatal
    }

    const char* path = nullptr;
    sd_bus_message_read(rep.m, "o", &path);
    if (path)
        m_seat0_path = path;
    LOG_DEBUG << "session_tracker: seat0 path=" << m_seat0_path;

    r = sd_bus_match_signal(m_bus, nullptr, "org.freedesktop.login1", m_seat0_path.c_str(),
                            "org.freedesktop.DBus.Properties", "PropertiesChanged",
                            on_seat_properties, this);
    if (r < 0)
        LOG_WARN << "session_tracker: match PropertiesChanged: " << strerror(-r);

    refresh_active_session();
    return true;
}

void SessionTracker::poll() {
    if (!m_bus)
        return;
    for (;;) {
        int r = sd_bus_process(m_bus, nullptr);
        if (r <= 0)
            break;
    }
}

int SessionTracker::bus_fd() const { return m_bus ? sd_bus_get_fd(m_bus) : -1; }

bool SessionTracker::is_active(const std::string& session_id) const {
    return !session_id.empty() && session_id == m_active_session_id;
}

int SessionTracker::handle_seat_properties(sd_bus_message* m) {
    // PropertiesChanged(interface, changed{}, invalidated[])
    const char* iface = nullptr;
    sd_bus_message_read(m, "s", &iface);

    if (!iface || strcmp(iface, "org.freedesktop.login1.Seat") != 0)
        return 0;

    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}") < 0)
        return 0;

    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &key);
        if (key && strcmp(key, "ActiveSession") == 0) {
            // variant: struct(ss) — (session_id, object_path)
            if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "(so)") >= 0) {
                if (sd_bus_message_enter_container(m, SD_BUS_TYPE_STRUCT, "so") >= 0) {
                    const char* sid = nullptr;
                    sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &sid);
                    if (sid) {
                        m_active_session_id = sid;
                        LOG_INFO << "session_tracker: active session → " << m_active_session_id;
                    }
                    sd_bus_message_exit_container(m);
                }
                sd_bus_message_exit_container(m);
            }
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    return 0;
}

void SessionTracker::refresh_active_session() {
    if (m_seat0_path.empty())
        return;

    struct Err {
        sd_bus_error e = SD_BUS_ERROR_NULL;
        ~Err() { sd_bus_error_free(&e); }
    } err;
    struct Msg {
        sd_bus_message* m = nullptr;
        ~Msg() { sd_bus_message_unref(m); }
    } rep;

    int r =
        sd_bus_get_property(m_bus, "org.freedesktop.login1", m_seat0_path.c_str(),
                            "org.freedesktop.login1.Seat", "ActiveSession", &err.e, &rep.m, "(so)");
    if (r < 0) {
        LOG_DEBUG << "session_tracker: read ActiveSession: "
                  << (err.e.message ? err.e.message : strerror(-r));
        return;
    }

    if (sd_bus_message_enter_container(rep.m, SD_BUS_TYPE_STRUCT, "so") >= 0) {
        const char* sid = nullptr;
        sd_bus_message_read_basic(rep.m, SD_BUS_TYPE_STRING, &sid);
        if (sid)
            m_active_session_id = sid;
        sd_bus_message_exit_container(rep.m);
    }
    LOG_INFO << "session_tracker: initial active session=" << m_active_session_id;
}

bool SessionTracker::session_is_active(const std::string& session_id) {
    if (session_id.empty())
        return false;
    return sd_session_is_active(session_id.c_str()) > 0;
}

} // namespace draind::daemon
