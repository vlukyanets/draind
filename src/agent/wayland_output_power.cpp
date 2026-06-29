#include "wayland_output_power.hpp"
#include "../shared/logger.hpp"

#include <cstring>

namespace draind::agent {

WaylandOutputPower::~WaylandOutputPower() {
    if (m_manager)
        zwlr_output_power_manager_v1_destroy(m_manager);
    for (auto* o : m_outputs)
        wl_output_destroy(o);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_display)
        wl_display_disconnect(m_display);
}

bool WaylandOutputPower::init() {
    const char* disp = getenv("WAYLAND_DISPLAY");
    if (!disp) {
        LOG_INFO << "output_power: WAYLAND_DISPLAY not set — skipping";
        return false;
    }

    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        LOG_INFO << "output_power: wl_display_connect failed — skipping";
        return false;
    }

    struct Ctx {
        zwlr_output_power_manager_v1* manager = nullptr;
        std::vector<wl_output*>       outputs;
    } ctx;

    static const wl_registry_listener kRegListener = {
        [](void* d, wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
            auto* c = static_cast<Ctx*>(d);
            if (strcmp(iface, zwlr_output_power_manager_v1_interface.name) == 0)
                c->manager = static_cast<zwlr_output_power_manager_v1*>(wl_registry_bind(
                    r, name, &zwlr_output_power_manager_v1_interface, std::min(ver, 1u)));
            else if (strcmp(iface, wl_output_interface.name) == 0)
                c->outputs.push_back(static_cast<wl_output*>(
                    wl_registry_bind(r, name, &wl_output_interface, std::min(ver, 4u))));
        },
        [](void*, wl_registry*, uint32_t) {},
    };

    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &kRegListener, &ctx);
    wl_display_roundtrip(m_display);

    if (!ctx.manager) {
        LOG_INFO << "output_power: compositor lacks zwlr_output_power_manager_v1 — skipping";
        return false;
    }
    if (ctx.outputs.empty()) {
        LOG_WARN << "output_power: no wl_output objects found";
        return false;
    }

    m_manager = ctx.manager;
    m_outputs = std::move(ctx.outputs);

    LOG_INFO << "output_power: initialized with " << m_outputs.size() << " output(s)";
    return true;
}

void WaylandOutputPower::set_mode(zwlr_output_power_v1_mode mode) {
    if (!m_manager)
        return;
    for (auto* output : m_outputs) {
        zwlr_output_power_v1* p =
            zwlr_output_power_manager_v1_get_output_power(m_manager, output);
        zwlr_output_power_v1_set_mode(p, mode);
        zwlr_output_power_v1_destroy(p);
    }
    wl_display_flush(m_display);
}

void WaylandOutputPower::set_off() {
    LOG_INFO << "output_power: DPMS off";
    set_mode(ZWLR_OUTPUT_POWER_V1_MODE_OFF);
}

void WaylandOutputPower::set_on() {
    LOG_INFO << "output_power: DPMS on";
    set_mode(ZWLR_OUTPUT_POWER_V1_MODE_ON);
}

} // namespace draind::agent
