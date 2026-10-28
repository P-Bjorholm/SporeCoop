// Test switches of the netcode fixes (`coop.netfix <name> 0|1`): 0 brings back the code before the
// fix, so a test can show that it still catches the bug. All on in normal play.
#pragma once

namespace net {

struct Fixes {
    bool big_reliable = true;  // BUG-0034: reliable messages above one packet are split, not stuck
    bool state_roster = true;  // BUG-0109: a guest ignores the state of a player not in its roster
    bool offset_window = true; // BUG-0133: the clock offset forgets an old minimum within seconds
    bool sender_time = true;   // BUG-0122: ship / vehicle velocity over the host's time, not arrival
};
inline Fixes g_fix;

} // namespace net
