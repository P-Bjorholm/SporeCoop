// Tribe stage co-op, "Chiefs" (docs/09-coop-design.md, RE notes docs/re/tribe.md):
//  - the host simulates every tribe on the planet; guests show mirrors of all citizens
//    (their own behaviour tree ticks are skipped, positions follow host snapshots)
//  - members of the player tribe are split between the players; a player can select and
//    order only their own members; guest orders are sent to the host and executed there
#pragma once

#include "syncdbg.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace tribe {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<void(const std::string&)> notice; // shown to every player
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx); // false: symbols missing
// own members selected now (the "give" button of the co-op window)
int own_selected();
// give the selected own members to another player (the host applies it); returns how many
int give_selected(uint8_t to);
void tick(double now);                               // every frame
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
void on_world_change();
std::string status();
std::vector<syncdbg::Entry> sync_entries(); // coop.sync (tests)
std::string list();
std::string debug(const std::vector<std::string>& args);
// owner of a player tribe member (player id), 0xFF if unknown
uint8_t owner_of(void* citizen);
bool active(); // tribe sync running (tribe stage, connected)
bool center(net::Vec3& pos); // position of the player tribe
// members of the player tribe with their owner (for name tags)
// the epic creature was hit by a creature (host; from the creature damage filter)
void note_animal_hit(void* animal, void* attacker_creature);
// a chief of a player (bigger, "★" tag)
// the member id (as in coop.tribe and coop.sync t<id>), 0 when not a tracked member
uint16_t id_of(void* citizen);
// tests: "hut<tribe index>[p]" for a tribe's hut, "" for anything else
std::string object_key(void* obj);
bool is_chief(void* citizen);
void for_each_owned(void (*fn)(void* citizen, uint8_t owner, void* user), void* user);

} // namespace tribe
