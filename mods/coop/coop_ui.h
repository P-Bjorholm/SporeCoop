// Co-op user interface built from Spore's own UI: a "Co-op" item in the main menu and a
// game dialog with two pages, "connect" (name, address, games on the network, hosting) and
// "lobby" (players, stage, ping, ready, chat, world transfer). docs/08-ui.md.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sporekit.h"

namespace coop_ui {

struct PlayerRow {
    uint8_t id = 0;
    std::string name;
    std::string stage; // "Creature", "Space, other world", ...
    bool host = false, me = false, ready = false;
    int ping_ms = -1;
};

struct LanGame {
    std::string name, address, info;
    int players = 0, max_players = 0;
};

// what the UI needs from the mod (filled in coop.cpp)
struct Api {
    std::function<std::string()> name;
    std::function<void(const std::string&)> set_name;
    std::function<bool(const std::string& address, std::string& err)> join;
    std::function<bool(uint16_t port, std::string& err)> host;
    std::function<void()> leave;
    std::function<int()> role;        // 0 offline, 1 host, 2 guest
    std::function<bool()> connected;
    std::function<std::string()> session_line;
    std::function<std::vector<PlayerRow>()> players;
    std::function<std::string()> world_line; // transfer progress / hints, "" if nothing
    std::function<bool()> ready;
    std::function<void(bool)> set_ready;
    std::function<void()> share_world; // host
    std::function<void(const std::string&)> say;
    std::function<std::vector<std::string>()> chat;
    std::function<std::vector<LanGame>()> lan_games;
    std::function<void(bool)> lan_scan; // start/stop looking for games on the network
    std::function<std::vector<std::string>()> local_addresses;
    std::function<uint32_t()> mode; // game mode id
    uint16_t default_port = 0;
};

void init(const SkHost* host, SkMod* mod, Api api);
void tick();     // every frame (inside Present): only schedules UI work outside the frame
void open(bool on);
bool is_open();
void shutdown(); // mod unload: hide our windows
std::string debug_state();
extern bool g_log_msgs; // debug: log UI messages reaching our handlers

} // namespace coop_ui
