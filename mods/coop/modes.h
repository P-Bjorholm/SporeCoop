// Per game mode adapters: what the local player controls in that mode and how a remote
// player is shown ("puppet"). docs/07-modes.md describes the design of every mode.
#pragma once

#include <cstdint>
#include <string>

#include "driver.h"
#include "net/protocol.h"

namespace modes {

constexpr uint32_t kCell = 0x1654C00, kCreature = 0x1654C01, kTribe = 0x1654C02, kCiv = 0x1654C04,
                   kSpace = 0x1654C05, kEdit = 0x1654C06, kLoad = 0x1654C08, kScenario = 0x1654C10,
                   kEditor = 0x00DBDBA1, kMenu = 0x2CCD1D2;

struct Adapter {
    const char* name;
    uint32_t mode;
    // where the local player is inside this mode: players see each other only when equal.
    // nullptr = the current save (players share the host's world)
    uint32_t (*place)();
    // true: guests keep their own saves (space: the galaxy is the same for everyone)
    bool own_worlds;
    // the entity the local player controls right now (nullptr if none)
    void* (*local)();
    // fill position/orientation/velocity/health/flags from the local entity
    void (*fill)(void* local, net::PlayerState& st);
    // create the visible copy of a remote player; `local` helps to pick a model/herd
    void* (*spawn)(const net::PlayerState& s, void* local);
    // move the puppet towards the remote state
    void (*drive)(void* puppet, const net::PlayerState& s, drive::State& ds, double now);
    void (*despawn)(void* puppet);
    // height of the name tag above the entity origin (nullptr: 3 m)
    float (*tag_height)(void* entity);
    // false when the game removed the puppet on its own (nullptr: always alive)
    bool (*alive)(void* puppet);
    // name tags go "up" along the planet normal; flat worlds (cell) use +z instead
    bool flat;
};

// position of any entity created/controlled by adapter `a`
net::Vec3 entity_pos(const Adapter* a, void* e);

const Adapter* for_mode(uint32_t mode);
const char* mode_name(uint32_t mode);
uint32_t current_world(); // crc32 of the current save name, 0 if none
uint32_t current_place(const Adapter* a); // adapter place() or current_world()
std::string current_world_name();

} // namespace modes
