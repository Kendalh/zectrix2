// What sim_platform.cc exposes to sim_device.cc.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sim {

struct PanelStats {
    int full = 0, partial = 0, gray = 0, errors = 0;
    std::string last;  // e.g. "partial 0,32,400,30"
    bool powered_off = false;  // deep sleep reached
    int total() const { return full + partial + gray; }
};

PanelStats panel_stats();
/** 8-bit gray copy of what the panel currently shows (400x300). */
std::vector<uint8_t> panel_pixels();

void net_set(bool up);       // station connected or not (fires on_change)
void net_fire_change();      // plain Wake
}  // namespace sim
