// One registry of the bug fixes' test switches (BUG-0116, docs/06-testing.md#testing-fix-registry).
//
// A test you never saw fail proves nothing (CLAUDE.md 3): a fix keeps its old behaviour behind a
// bool, on (the fixed code) by default. Each module registers its bool next to its definition:
//
//   static bool g_fix_x = true;
//   COOP_FIX (<"area.switch">, <"BUG-NNNN">, g_fix_x, <"one line: what the fix does">);
//
// and the console command `coop.fix` switches it (`coop.fix list`, `coop.fix <name> on|off`,
// `coop.fix all on|off`). The registry only points at the bools: the old per-module commands
// (`coop.pool fix purge 0`, `coop.npc strays off`, ...) keep working on the same bools.
// Names are `<area>.<switch>`, the area being the old command's (pool, npc, civ, tribe, ...).
// Portable (no Windows, no SporeKit): tests/fixes_tests.cpp runs it natively. SporeKit's own fixes
// stay under `kit.fix` (another DLL).
#pragma once

#include <string>
#include <vector>

namespace fix {

struct Info {
    const char* name;   // <area>.<switch>, unique
    const char* ticket; // BUG-NNNN (several: "BUG-0001 BUG-0002")
    bool* on;           // true = the fixed code (default), false = the behaviour before the fix
    const char* what;   // one line: what the fix does
};

// register a switch (static initialisation: COOP_FIX). A second registration of a name, or a
// null pointer, is refused (counted in duplicates(), shown by list()). Returns whether it was taken.
bool reg(const char* name, const char* ticket, bool* on, const char* what);
const std::vector<Info>& all(); // in the order of registration
const Info* find(const std::string& name);
bool set(const std::string& name, bool on); // false: unknown name
size_t set_all(bool on);                    // every switch; returns how many
int duplicates();
std::string list(); // "N fixes (M off)" then one line per fix: name on|off ticket what
// the coop.fix command over its arguments (argv without the command name); *ok = false on a usage
// error or an unknown name
std::string command(const std::vector<std::string>& args, bool* ok = nullptr);

} // namespace fix

#define COOP_FIX_CAT2(a, b) a##b
#define COOP_FIX_CAT(a, b) COOP_FIX_CAT2(a, b)
// one line next to the switch's bool (at namespace scope, in one .cpp file only)
#define COOP_FIX(name, ticket, var, what)                                                                             \
    [[maybe_unused]] static const bool COOP_FIX_CAT(coop_fix_reg_, __LINE__) = ::fix::reg(name, ticket, &(var), what)
