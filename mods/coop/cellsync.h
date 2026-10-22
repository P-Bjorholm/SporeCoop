// Cell stage co-op, "Symbiosis" (docs/09-coop-design.md, RE notes docs/re/cell.md):
//  - one tide pool: the host simulates NPC cells and food, guests show passive mirrors
//    (no local spawning, no local AI) and send their bites / eaten food to the host
//  - food progress is shared (the host keeps the total)
//  - a dying player is saved by a partner nearby: respawns next to them instead of
//    restarting the level; health regenerates while a partner is close
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace cellsync {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<std::vector<std::pair<std::string, net::Vec3>>()> partners; // same place
    std::function<void(const std::string&)> notice;
    std::function<std::string()> my_name;
    std::function<int(void* cell)> puppet_owner; // host: player id of a puppet cell, -1 if none
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
std::string status();
std::string debug(const std::vector<std::string>& args);

} // namespace cellsync
