---
id: BUG-0099
title: the joint-hunt hit table grows without bound and keys hits by reusable creature addresses
status: open
kind: bug
stage: Creature
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#77
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #77
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/77 (full text, discussion).

teamplay keeps recent player hits per target creature in a `std::map<void*, std::vector<Hit>>`. Old hits are pruned only for the target being hit right now. The size cleanup only removes entries that are already empty, which never happens for targets nobody hits again. In a long creature-stage session, every creature ever hit stays in the map. After 256 entries, every hit walks the whole map and removes nothing. Keys are raw creature addresses, so a new creature allocated where a dead one was inherits its recent hits. Severity: minor (slow growth; a rare wrong joint-hunt bonus).

### Where
https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/teamplay.cpp#L35-L58

```cpp
auto& v = g_hits[target];
for (...) if (now - v[i].t > kHuntWindow) v.erase(...);   // only this target's hits
v.push_back({player, now});
if (g_hits.size() > 256)
    for (...) it = it->second.empty() ? g_hits.erase(it) : std::next(it);  // others are never empty
```

### How to reproduce
Creature stage, two players hunting for an hour (or a test bot hitting many NPCs: `coop.npc hosthit` over many ids). There is no status output for the map size; a debug print of `g_hits.size()` keeps rising past 256.

### Expected
The table holds only hits within `kHuntWindow`, and is cleared on a world change and at session end.

## Root cause
**What happens**

1. Each first hit on a creature adds an entry with one hit.
2. That entry's vector is only pruned when the same creature is hit again, so for a killed creature it keeps its last hit forever.
3. Past 256 entries the cleanup loop runs on every hit, finds no empty vector, and the map keeps growing (one entry per creature ever hit, for the whole session: no session end, stage change or `on_world_change` clears it).
4. A creature freed by the game and a new one allocated at the same address within `kHuntWindow` gets a "joint" bonus from the other player's hit on the dead one.

### Confidence
CONFIRMED by reading.

## Fix
Not fixed yet. Suggested in the review:

In the size cleanup, prune each vector by `kHuntWindow` and then erase the empty ones (or prune every second from `tick`). Clear `g_hits` on a world change and at session end (#11). Optionally key by creature plus a generation (or check `game::creature_listed` when reading).

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tests/net_tests.cpp`-style host test, since teamplay's map logic is portable if `H->time_seconds` is stubbed: hit 1000 distinct targets with time advancing past `kHuntWindow`, then assert the map size stays at most the number hit within the window. It fails on main.

### Related
- Umbrella #11 (module state that no session end clears).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #77.
- 2026-10-27: ticket opened from the issue.
