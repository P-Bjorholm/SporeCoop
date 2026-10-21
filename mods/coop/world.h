// World sharing between host and guests (save folder transfer).
#pragma once

#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace world {

void init(const SkHost* host, SkMod* mod, std::function<void(const std::string&)> feed);
// host: queue the current save folder for a guest
bool share_with(uint8_t peer, std::string& err);
// host: stream queued transfers (call every frame)
void tick(net::Session& s);
// guest: handle MSG_WORLD_* messages
void on_message(uint8_t type, net::Reader& r, const std::string& host_name);
std::string status();
void set_auto_load(bool on);
bool load_slot(const std::wstring& slot, const std::string& planet_file, std::string& err);

} // namespace world
