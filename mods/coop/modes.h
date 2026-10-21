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
    // the entity the local player controls right now (nullptr if none)
    void* (*local)();
    // fill position/orientation/velocity/health/flags from the local entity
    void (*fill)(void* local, net::PlayerState& st);
    // create the visible copy of a remote player; `local` helps to pick a model/herd
    void* (*spawn)(const net::PlayerState& s, void* local);
    // move the puppet towards the remote state
    void (*drive)(void* puppet, const net::PlayerState& s, drive::State& ds, double now);
    void (*despawn)(void* puppet);
};

const Adapter* for_mode(uint32_t mode);
const char* mode_name(uint32_t mode);
uint32_t current_world(); // crc32 of the current save name, 0 if none
std::string current_world_name();

} // namespace modes
