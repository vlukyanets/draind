#pragma once
#include <functional>
#include <string>
#include <systemd/sd-bus.h>
#include <unordered_map>
#include <unordered_set>

namespace draind {

// Watches MPRIS2 players on the session D-Bus.
// Calls on_inhibit(reason) when any player is Playing,
// and on_uninhibit(reason) when all players stop.
class MprisMonitor {
  public:
    using InhibitCb = std::function<void(const std::string& reason)>;

    MprisMonitor();
    ~MprisMonitor();

    MprisMonitor(const MprisMonitor&)            = delete;
    MprisMonitor& operator=(const MprisMonitor&) = delete;

    bool init();
    void poll();
    int  bus_fd() const;

    void on_inhibit(InhibitCb cb)   { m_on_inhibit   = std::move(cb); }
    void on_uninhibit(InhibitCb cb) { m_on_uninhibit = std::move(cb); }

    // Called by sd-bus trampoline
    int handle_name_owner_changed(sd_bus_message* m);
    int handle_properties_changed(sd_bus_message* m);

  private:
    static constexpr const char* k_inhibit_reason = "mpris-playing";

    void scan_existing_players();
    void add_player(const std::string& well_known, const std::string& unique);
    void remove_player(const std::string& well_known);
    void query_playback_status(const std::string& unique);
    bool is_mpris_name(const std::string& name) const;
    void set_player_playing(const std::string& unique, bool playing);

    sd_bus* m_bus = nullptr;

    // Maps well-known name → unique name and vice versa
    std::unordered_map<std::string, std::string> m_wk_to_unique; // well-known → unique
    std::unordered_map<std::string, std::string> m_unique_to_wk; // unique → well-known

    // Set of unique bus names currently Playing
    std::unordered_set<std::string> m_playing;

    bool m_inhibiting = false;

    InhibitCb m_on_inhibit;
    InhibitCb m_on_uninhibit;
};

} // namespace draind
