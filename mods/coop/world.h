// World sharing between host and guests (save folder transfer).
#pragma once

#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace world {

void init(const SkHost* host, SkMod* mod, std::function<void(const std::string&)> feed);
// host: queue the current save folder for a guest (optionally save the game first)
bool share_with(uint8_t peer, std::string& err, bool save_first = false);
bool share_now(uint8_t peer, std::string& err);
// the game's own save (pause menu "Save"); also works on planets in space
bool save_game(std::string& err);
// host: stream queued transfers (call every frame)
void tick(net::Session& s);
// guest: handle MSG_WORLD_* messages
void on_message(uint8_t type, net::Reader& r, const std::string& host_name);
std::string status();
// guest: a world transfer in progress (bytes so far / total)
bool progress(uint32_t& got, uint32_t& total);
void set_auto_load(bool on);
bool load_slot(const std::wstring& slot, const std::string& planet_file, std::string& err);
// load Games\<slot_name>\<planet>.spo of the local profile (works from the galaxy menu)
bool load_local(const std::string& slot_name, const std::string& planet, std::string& err);

} // namespace world
