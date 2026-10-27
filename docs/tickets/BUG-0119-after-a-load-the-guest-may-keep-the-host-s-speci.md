---
id: BUG-0119
title: after a load the guest may keep the host's species, or get SetSpecies on a world still loading (avatar pointer used as world identity)
status: open
kind: bug
stage: Creature
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#98
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #98
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/98 (full text, discussion).

Three places tell "a new world" from "the same world" only by comparing the avatar's address. Their remembered pointer is never reset when the creature stage is left within a session: a load, a world received from the host, or the menu.

The game pools creature objects (`coop.npc creatures` counts creature objects "without a species: not in the world (pool)"). So the avatar of the next world can have the old address. Each check then takes the new world for the old one.

`coop.cpp` already does it right for its own editor timer: `g_settle_avatar` goes back to null in every frame outside the creature stage.

### Where
- Own avatar swap: `g_seen_avatar` (3 s settle guard) and `g_swapped_avatar` (swap done) are compared by pointer and only updated in the creature stage. `g_swapped_avatar` is reset on session end only, and `g_seen_avatar` never.
  - Store: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L776-L780
  - Use: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L828-L863
  - Session-end reset: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L1004
- Guest proxies: `static s_av`, the "first 1 s of a world" wait. It is not updated while there is no avatar (the return comes first): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L390-L403
- Deferred destroys: the world token is the noun manager's avatar pointer. A queued creature is destroyed in a later world if the avatar address matches and the queued address is listed again: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L414-L468

### How to reproduce
1. A guest with its own creature in the host's creature world, then any reload within the session: the host's editor sends a new world, or `coop.load`.
2. Log `game::avatar()` before and after the load, on the guest.
3. If the address repeats, `coop.status` (creations swaps counter) shows no new swap, and the guest's avatar species stays the host's (`species_key` of the avatar).

To see the effect without relying on the allocator: in a test build, keep `g_seen_avatar` and `g_swapped_avatar` pointed at a fixed address and reuse it.

### Expected
After every load the own creature is applied again, and only once the new world has played for 3 s.

## Root cause
**What happens (if the new avatar has the old address)**

1. `apply_own_avatar` sees `av == g_seen_avatar` and skips the 3 s wait. `SetSpecies` then runs on a world that is still loading. The comment there says that crashed ("a null call inside the game").
2. Or `av == g_swapped_avatar && k == g_swapped_key` returns early. The own-creature swap is never applied in the reloaded world, so the guest keeps playing as the creature the save gives it, typically the host's species.
3. npc's `s_av` skips its 1 s wait. Proxies are created before the update filter has learned the nest herds (`g_local_herds`), so they go into new herds of ours instead of the species' nest herds: no archetype tuning (+0xE84), and more herds of ours in later saves.
4. A creature queued in `g_destroy_later` during a cinematic just before a load would pass the token check if its address is listed again in the new world. A random creature of the new world would then be destroyed. The avatar itself is not excluded.

### Confidence
- PLAUSIBLE: the code paths are confirmed by reading, but whether the game really gives the avatar of a reloaded world the old address was not checked at runtime.
- A second reviewer verified the reading and rated it low.

## Fix
Not fixed yet. Suggested in the review:

- Reset `g_seen_avatar`, `g_swapped_avatar` and npc's `s_av` in every frame outside the creature stage, as `own_editor_tick` does with `g_settle_avatar`.
- Better: a world counter that `coop.cpp on_frame` bumps where it detects a world change (`g_local != g_last_avatar`, or `g_local` becoming null). These modules and `game::destroy_creature` would store that counter instead of the avatar pointer.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Two-instance creature test: the guest has an own creature, then the host reloads the world twice. After each load, assert that the guest's avatar species key (read from the game) is the guest's own creation. Also assert that the swap happens at least 3 s after the avatar appeared.

- FAILS without the fix: force the address match with a debug switch that keeps the old pointers.
- PASSES with it.

### Related
#22 (recycled NPC objects keep old ids), #44 (deferred removal during cinematics), #54, #69.

This came from an inventory of about 90 game-object pointers the mod keeps across frames, 24 of them used as map keys. This one is the only identity check that relies on the avatar address.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #98.
- 2026-10-27: ticket opened from the issue.
