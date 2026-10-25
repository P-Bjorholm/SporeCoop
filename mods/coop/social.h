// Creature stage: social games of guests with NPCs (docs/re/creature.md 8, docs/09-coop-design.md).
//
// The host simulates the NPCs; a guest sees AI-less mirrors (npc.cpp). A social game is run by the
// NPC's own behaviour tree (node 0x341BD4BE "SocialPropose"), so a mirror never answered the
// guest. The guest's social actions on a mirror go to the host, the host counts them like the
// mini-game's rounds, applies the native result on the real NPC (impress event of the species
// relationship manager, DNA, the creature's "impressed" flag) and sends the outcome back. The
// species relationship table (impressed / killed per species, ally / extinct) is the host's and is
// mirrored to the guests, so the goal cards of both show the same.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace social {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;      // own notification
    std::function<void(const std::string&)> notice;    // everybody's notification
    std::function<void*(uint8_t id)> puppet_of;        // host: the creature of player `id`
    std::function<bool()> blocked;                     // a menu / the co-op window takes the keys
};

void init(const SkHost* host, SkMod* mod, const Ctx& ctx);
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_session_end();
// a DNA gain paid by a species goal card (allied / extinct): the host pays it once per species and
// outcome, whichever machine's card saw it; true = swallow it here
bool goal_dna(float delta);
std::string status();
std::string species_dump(); // coop.social species (tests)
bool debug_set_record(uint32_t crt_instance, int relation, uint32_t impressed); // host, tests: a record of the table
// guest: a social action (ability slot 0-3) on proxy #id as if done in game; slot -1 toggles the
// stimulus log
bool debug_social(uint16_t npc_id, int slot);
bool debug_goto(uint16_t npc_id); // tests: the guest's avatar steps next to proxy #id
std::string debug_objects(uint32_t vt_lo, uint32_t vt_hi); // game objects with a vtable in the range

} // namespace social
