// M2 "pack": host-authoritative NPC creatures (docs/03-gameplay.md 3.3, docs/04-netcode.md 4.5).
//
// Host : streams NPCs near each guest (spawn/state/despawn), applies guests' hits.
// Guest: removes its own NPCs, mirrors the host's NPCs as AI-less proxies, forwards
//        hits on proxies to the host and takes damage the host reports.
#pragma once

#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace npc {

struct Ctx {
    net::Session* sess = nullptr;
    // creature that represents player `id` locally (host: guest puppet), or nullptr
    std::function<void*(uint8_t id)> puppet_of;
    // last known world position of player `id`; false if not in the world
    std::function<bool(uint8_t id, net::Vec3& pos)> player_pos;
    std::function<void(const std::string&)> feed;
    // RTS stages (no avatar): the shared centre (player tribe) all players are near
    std::function<bool(net::Vec3& pos)> stage_center;
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now);                        // every frame (also when offline)
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();                        // destroy proxies, restore local NPCs
void on_world_change();                       // world unloaded: forget (not destroy) proxies
bool is_proxy(void* creature);
// TakeDamage filter shared with coop.cpp; returns true to let the game apply damage
bool debug_host_hit(uint16_t id, float damage); // host avatar hits host NPC #id
void* proxy(uint16_t id);                       // guest: local creature mirroring host NPC #id
bool damage_filter(void* creature, float& damage, int type, void* attacker);
std::string status();
std::string list();                                  // debug: mirrored NPCs
bool debug_hit(uint16_t id, float damage);           // guest: avatar hits proxy `id`
bool debug_hurt(uint8_t player, uint16_t id, float damage); // host: NPC `id` hits player's puppet
void set_enabled(bool on);
bool enabled();

} // namespace npc
