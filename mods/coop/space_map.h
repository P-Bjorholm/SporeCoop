// Space feature "map" (space_map.cpp, MSG_SPACE kinds 10..19): partners and their colonies on
// the galaxy map, and the ROSTER every other Space feature reads.
//
// Roster: every player broadcasts a summary of its own empire every few seconds and at once on a
// change (home star, where it is, empire name and colour, its colonies). Each machine keeps the
// latest complete roster of every player, its own included (roster_of(my_id)). A player who joins
// (or rejoins) asks the others for theirs and has them within a second; anyway within one period.
// Rosters of players who left are dropped. Star and planet ids are the same in every copy of the
// galaxy (docs/re/space.md 7.3), so a consumer can resolve them in its own save
// (cStarManager::GetStarRecord / GetPlanetRecord).
//
// Consumers (features 4 allied colonies, 5 trade, 7 contracts, 10 rivals): call roster_of /
// roster_ids whenever needed (cheap, main thread only: the Present thread that runs ticks,
// messages and the overlay), and either poll roster_changes() or register on_roster_change().
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/protocol.h"

namespace spacemap {

struct Colony {
    uint32_t star = 0;        // StarID (cStarRecord +0x70)
    uint32_t planet = 0;      // PlanetID (cPlanetRecord +0x184): star in the low 24 bits, index << 24
    std::string name;         // planet name (UTF-8, "" if not readable)
    std::string star_name;    // the star's name in the owner's game (the names of stars nobody named are
                              // made up anew in every game: another copy may call it differently)
    uint32_t spice[3] = {};   // the planet's spice ResourceKey {instance, type, group} (record +0x198), 0 = none
    std::string spice_name;   // "" if not known
    uint16_t cities = 0;      // cities of the owner's empire on the planet (cCivData +0x3C)
    bool home = false;        // the empire's home planet
};

struct Roster {
    uint8_t id = 0;           // session player id
    uint32_t home_star = 0;   // StarID of the home star, 0 = not known
    uint32_t home_planet = 0; // PlanetID of the home planet, 0 = not known
    uint32_t place = 0;       // modes place: StarID | 0x80000000, PlanetID | 0x40000000, 0 = galaxy view
    net::Vec3 galaxy_pos{};   // the ship in galaxy coordinates (the current star's position in a system)
    uint32_t star_now = 0;    // StarID the player is at (in a system / on a planet), 0 = galaxy view
    std::string star_now_name;// its name in the player's game (see Colony::star_name)
    std::string empire_name;  // UTF-8, "" if not readable
    uint32_t color = 0;       // ARGB of the empire's identity colour, 0 = not readable
    uint32_t political_id = 0;// the empire's political id in its own save (not valid in other saves)
    int money = 0;            // the empire's money (shared mode: everybody's is the same)
    std::vector<Colony> colonies;
    double updated = 0;       // local time (SkHost::time_seconds) the roster was last completed
    uint32_t version = 0;     // the sender's roster revision (grows on every change)
};

// the roster of player id (my_id included), nullptr if none yet
const Roster* roster_of(uint8_t id);
// every player with a roster (my own first when I have one)
std::vector<uint8_t> roster_ids();
// grows whenever any roster changes (a new one, a change, a removal): poll and compare
uint32_t roster_changes();
// called (main thread) after roster `id` changed or was removed (then roster_of(id) == nullptr)
void on_roster_change(std::function<void(uint8_t id)> f);

// the colour the mod shows player id in (the name tag, the colony marks): its empire colour,
// unless that is too close to mine (both are often the game's "player" colour), then one of a
// fixed palette
uint32_t player_color(uint8_t id);
// the name of a star in my own game (UTF-8), "" if not readable
std::string star_name(uint32_t star);
// a partner's colony that is not one of mine: the players start as one empire (the guests load the
// host's save), so the colonies both have are shared, the others are that player's own
bool own_colony(uint8_t id, const Colony& c);

} // namespace spacemap
