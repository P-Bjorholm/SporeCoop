// Received worlds on disk (world.cpp): which save slot (Games\GameN) a host's world goes into and
// how it is written. Only Win32 file calls, no game: tests/win_tests.cpp runs it under Wine.
//
// * pick: never the slot the game plays (the folder the save database points to, BUG-0035), and a
//   co-op slot is reused only for the same host world (host name + planet in its sporecoop.txt,
//   BUG-0087); else a free GameN.
// * write: every file goes into a fresh folder next to Games first (Games-sporecoop-tmp), each
//   write checked; only when all of them are complete the folder replaces the slot as a whole and
//   carries the marker (BUG-0105). An old copy of the same world is replaced entirely, so no file
//   of another world stays next to it (BUG-0087).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace slotfs {

struct File {
    std::string rel; // '/' separated, checked by the caller (world.cpp safe_rel)
    std::vector<uint8_t> data;
};

// tests: false = the old behaviour (`coop.worldfix busy_slot|slot_match|write_check 0`)
extern bool g_busy_fix, g_match_fix, g_write_fix;

// games: ...\Spore\Games. busy_lower: the slot the game plays (lower case, no trailing '\'), may be
// empty. backup_planet: move another slot holding planet_spo out of Games first (menu loading,
// auto_load_world=0, needs the planet's disc free); `note` then says where it went.
std::wstring pick(const std::wstring& games, const std::wstring& busy_lower, const std::string& host,
                  const std::string& planet, const std::string& planet_spo, bool backup_planet, std::string& note);

// writes the world into `slot` (replacing what is there); false and `err` when anything failed: the
// slot is then as it was before
bool write(const std::wstring& slot, const std::vector<const File*>& files, const std::string& host,
           const std::string& planet, uint32_t world_id, std::string& err);

std::string narrow(const std::wstring& w);
std::wstring widen(const std::string& s);

} // namespace slotfs
