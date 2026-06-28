#include "mpris_monitor.hpp"
#include "../shared/logger.hpp"

#include <cstring>
#include <string>

namespace draind::agent {

static int trampoline_name_owner(sd_bus_message* m, void* ud, sd_bus_error*) {
    return static_cast<MprisMonitor*>(ud)->handle_name_owner_changed(m);
}
static int trampoline_properties(sd_bus_message* m, void* ud, sd_bus_error*) {
    return static_cast<MprisMonitor*>(ud)->handle_properties_changed(m);
}

MprisMonitor::MprisMonitor() = default;
MprisMonitor::~MprisMonitor() {
    if (m_bus)
        sd_bus_unref(m_bus);
}

bool MprisMonitor::init() {
    int r = sd_bus_open_user(&m_bus);
    if (r < 0) {
        LOG_ERROR << "mpris: sd_bus_open_user: " << strerror(-r);
        return false;
    }

    // Watch NameOwnerChanged to detect players appearing/disappearing
    r = sd_bus_match_signal(m_bus, nullptr, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                            "org.freedesktop.DBus", "NameOwnerChanged", trampoline_name_owner,
                            this);
    if (r < 0) {
        LOG_WARN << "mpris: match NameOwnerChanged: " << strerror(-r);
        return false;
    }

    // Watch PropertiesChanged on all MPRIS objects
    r = sd_bus_match_signal(m_bus, nullptr, nullptr, "/org/mpris/MediaPlayer2",
                            "org.freedesktop.DBus.Properties", "PropertiesChanged",
                            trampoline_properties, this);
    if (r < 0) {
        LOG_WARN << "mpris: match PropertiesChanged: " << strerror(-r);
        return false;
    }

    scan_existing_players();
    LOG_DEBUG << "mpris: initialized";
    return true;
}

void MprisMonitor::poll() {
    if (!m_bus)
        return;
    for (;;) {
        int r = sd_bus_process(m_bus, nullptr);
        if (r <= 0)
            break;
    }
}

int MprisMonitor::bus_fd() const { return m_bus ? sd_bus_get_fd(m_bus) : -1; }

bool MprisMonitor::is_mpris_name(const std::string& name) const {
    return name.rfind("org.mpris.MediaPlayer2.", 0) == 0;
}

void MprisMonitor::scan_existing_players() {
    struct Err {
        sd_bus_error e = SD_BUS_ERROR_NULL;
        ~Err() { sd_bus_error_free(&e); }
    } err;
    struct Msg {
        sd_bus_message* m = nullptr;
        ~Msg() { sd_bus_message_unref(m); }
    } rep;

    int r = sd_bus_call_method(m_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                               "org.freedesktop.DBus", "ListNames", &err.e, &rep.m, "");
    if (r < 0) {
        LOG_WARN << "mpris: ListNames: " << (err.e.message ? err.e.message : strerror(-r));
        return;
    }

    char** names = nullptr;
    sd_bus_message_read_strv(rep.m, &names);
    if (!names)
        return;

    for (int i = 0; names[i]; ++i) {
        std::string name = names[i];
        if (!is_mpris_name(name))
            continue;

        // Resolve unique name
        struct Err2 {
            sd_bus_error e = SD_BUS_ERROR_NULL;
            ~Err2() { sd_bus_error_free(&e); }
        } err2;
        struct Msg2 {
            sd_bus_message* m = nullptr;
            ~Msg2() { sd_bus_message_unref(m); }
        } rep2;
        r = sd_bus_call_method(m_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                               "org.freedesktop.DBus", "GetNameOwner", &err2.e, &rep2.m, "s",
                               name.c_str());
        if (r >= 0) {
            const char* unique = nullptr;
            sd_bus_message_read(rep2.m, "s", &unique);
            if (unique)
                add_player(name, unique);
        }
        free(names[i]);
    }
    free(names);
}

void MprisMonitor::add_player(const std::string& well_known, const std::string& unique) {
    m_wk_to_unique[well_known] = unique;
    m_unique_to_wk[unique]     = well_known;
    LOG_DEBUG << "mpris: player appeared: " << well_known << " (" << unique << ")";
    query_playback_status(unique);
}

void MprisMonitor::remove_player(const std::string& well_known) {
    auto it = m_wk_to_unique.find(well_known);
    if (it == m_wk_to_unique.end())
        return;
    std::string unique = it->second;
    LOG_DEBUG << "mpris: player gone: " << well_known;
    m_wk_to_unique.erase(it);
    m_unique_to_wk.erase(unique);
    set_player_playing(unique, false);
}

void MprisMonitor::query_playback_status(const std::string& unique) {
    struct Err {
        sd_bus_error e = SD_BUS_ERROR_NULL;
        ~Err() { sd_bus_error_free(&e); }
    } err;
    struct Msg {
        sd_bus_message* m = nullptr;
        ~Msg() { sd_bus_message_unref(m); }
    } rep;

    int r =
        sd_bus_get_property(m_bus, unique.c_str(), "/org/mpris/MediaPlayer2",
                            "org.mpris.MediaPlayer2.Player", "PlaybackStatus", &err.e, &rep.m, "s");
    if (r < 0) {
        LOG_DEBUG << "mpris: query PlaybackStatus from " << unique << ": "
                  << (err.e.message ? err.e.message : strerror(-r));
        return;
    }

    const char* status = nullptr;
    sd_bus_message_read(rep.m, "s", &status);
    if (status) {
        bool playing = strcmp(status, "Playing") == 0;
        LOG_DEBUG << "mpris: " << unique << " PlaybackStatus=" << status;
        set_player_playing(unique, playing);
    }
}

void MprisMonitor::set_player_playing(const std::string& unique, bool playing) {
    if (playing)
        m_playing.insert(unique);
    else
        m_playing.erase(unique);

    bool any_playing = !m_playing.empty();
    if (any_playing && !m_inhibiting) {
        m_inhibiting = true;
        if (m_on_inhibit)
            m_on_inhibit(k_inhibit_reason);
    } else if (!any_playing && m_inhibiting) {
        m_inhibiting = false;
        if (m_on_uninhibit)
            m_on_uninhibit(k_inhibit_reason);
    }
}

int MprisMonitor::handle_name_owner_changed(sd_bus_message* m) {
    const char* name      = nullptr;
    const char* old_owner = nullptr;
    const char* new_owner = nullptr;
    sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner);
    if (!name)
        return 0;

    std::string sname = name;
    if (!is_mpris_name(sname))
        return 0;

    if (new_owner && *new_owner)
        add_player(sname, new_owner);
    else
        remove_player(sname);

    return 0;
}

int MprisMonitor::handle_properties_changed(sd_bus_message* m) {
    // Determine which player sent this by looking up the sender's unique name
    const char* sender = sd_bus_message_get_sender(m);
    if (!sender)
        return 0;

    std::string unique = sender;
    if (m_unique_to_wk.find(unique) == m_unique_to_wk.end())
        return 0;

    // Read iface
    const char* iface = nullptr;
    sd_bus_message_read(m, "s", &iface);
    if (!iface || strcmp(iface, "org.mpris.MediaPlayer2.Player") != 0)
        return 0;

    // Scan changed{} for PlaybackStatus
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}") < 0)
        return 0;

    while (sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &key);
        if (key && strcmp(key, "PlaybackStatus") == 0) {
            if (sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "s") >= 0) {
                const char* val = nullptr;
                sd_bus_message_read_basic(m, SD_BUS_TYPE_STRING, &val);
                if (val) {
                    bool playing = strcmp(val, "Playing") == 0;
                    LOG_DEBUG << "mpris: " << unique << " PlaybackStatus → " << val;
                    set_player_playing(unique, playing);
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

} // namespace draind::agent
