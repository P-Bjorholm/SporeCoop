// Sync measurement (tests): every synchronized object with a key shared by the host and the
// guests, its position and orientation. The console command coop.sync prints them; the test
// tools/tests/coop_sync.py compares host and guest lists in every stage.
#pragma once

#include <string>
#include <vector>

#include "net/interp.h"

namespace syncdbg {

struct Entry {
    std::string key; // p<player>, n<npc>, t<citizen>, v<vehicle>, s<ship>, c<cell>
    net::Vec3 pos;
    net::Quat rot;
};

// "key x y z qx qy qz qw" per line
std::string format(const std::vector<Entry>& v);

} // namespace syncdbg
