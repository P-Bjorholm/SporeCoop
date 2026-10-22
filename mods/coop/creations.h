// Every player keeps their own creations (creature, cell, spaceship): docs/re/creations.md.
//  - own creation: picked from the player's My Creations (EditorSaves.package); a guest defaults to
//    its newest creation, the host to the species of its world
//  - its records (bem + metadata) go to everybody (the host relays them and sends them to players
//    joining later); a receiver stores them under a collision-free instance in its own small
//    package and registers it with the game's resource manager at runtime
//  - puppets of a player are built from that player's creation; the local avatar takes the own one
//    (abilities follow the parts)
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/dbpf.h"
#include "net/session.h"
#include "sporekit.h"

namespace creations {

enum Kind : uint8_t { kCreature = 0, kCell = 1, kUfo = 2, kKinds = 3 };
using Key = dbpf::Key;

struct Option {
    Key key;
    std::string name;
};

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<std::string(uint8_t)> name_of;
    std::function<void(uint8_t player)> appearance_changed; // rebuild that player's puppet
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void on_message(uint8_t type, uint8_t from, net::Reader& r);
void on_player_join(uint8_t id); // host: everybody's creations to the newcomer
void on_session_end();

// the creation of a player, usable on this machine ({} = none / not received yet)
Key player_key(uint8_t player, Kind k);
// cSpeciesProfile* of a player's creature, nullptr if none
void* player_species(uint8_t player);

// My Creations of this kind, newest first
std::vector<Option> options(Kind k);
Option mine(Kind k);
void choose(Kind k, const Key& key); // {} = keep the world's own
// UI: the next / previous own creation
void cycle(Kind k, int dir);

std::string status();
std::string debug(const std::vector<std::string>& args);

} // namespace creations
