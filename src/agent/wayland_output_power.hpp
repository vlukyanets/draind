#pragma once
#include <wayland-client.h>
#include "wlr-output-power-management-unstable-v1-client-protocol.h"
#include <vector>

namespace draind::agent {

// Controls display power (DPMS on/off) via zwlr_output_power_manager_v1.
// Returns false from init() if the compositor lacks the protocol — caller
// should log and continue without screen-off support.
class WaylandOutputPower {
  public:
    ~WaylandOutputPower();

    // Connect to the Wayland compositor and bind the output power protocol.
    bool init();

    // Set all outputs to DPMS off / on. Flushed immediately.
    void set_off();
    void set_on();

  private:
    void set_mode(zwlr_output_power_v1_mode mode);

    wl_display*                  m_display = nullptr;
    wl_registry*                 m_registry = nullptr;
    zwlr_output_power_manager_v1* m_manager = nullptr;
    std::vector<wl_output*>      m_outputs;
};

} // namespace draind::agent
