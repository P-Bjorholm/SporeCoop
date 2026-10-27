---
id: BUG-0082
title: the guest keeps creature pointers while the host is away and destroys them minutes later
status: open
kind: crash
stage: Creature
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#60
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #60
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/60 (full text, discussion).

On a guest, the update filter collects the game's own NPCs into `g_kill` during the game's update, and `guest_tick` destroys them in the same frame. On the frame the host goes away (to the main menu), `guest_tick` is skipped and `g_kill` is not cleared. Those pointers are only used when the host comes back to the same world, possibly minutes later, by which time the objects may have been freed or reused. Severity: possible crash (a freed object used later, the most common crash class in CLAUDE.md section 2). The window is one frame, so it is rare.

### Where
- Filter fills `g_kill`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L93-L106
- `g_kill` is used and cleared only in `guest_tick`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L380-L391
- The away branch skips `guest_tick`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L583-L601
- `g_host_away` flips in `host_away_tick`, later in the frame than the filter: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L369-L382

### How to reproduce
1. Two instances, Creature stage, NPC sync on.
2. Make the guest's game have local NPCs updating on the flip frame: for example, start a cinematic on the guest (deferred destroys are re-pushed every frame), or stand near a nest that refills.
3. The host goes to the main menu (pause menu, Quit to galaxy) and stays there for more than 1.5 s. Check `coop.npc` on the guest: `host_away=1`.
4. On the guest, kill and eat the NPCs that were near; wait until their corpses are gone.
5. The host loads the same save again (back to the same world). The guest's first `guest_tick` uses the old pointers. A debug log of `g_kill.size()` at that moment shows entries from before the away time.

### Expected
`g_kill` holds only the pointers of the current frame; nothing collected before the host went away is used after it is back.

## Root cause
**What happens**

1. Frame N: `host_away_tick` (coop.cpp) sets `g_host_away = true` after the host has been in the main menu for 1.5 s.
2. Frame N+1, in the game's update: npc.cpp's own `g_host_away` is still false, so `update_filter` pushes every local NPC that tries to `Update` into `g_kill`. Creatures whose destroy waits for a cinematic (`destroy_pending`) are pushed every frame too.
3. Frame N+1, `npc::tick`: `away` is true, so the proxies are destroyed, `g_host_away = true`, and `guest_tick` (the only place that reads and clears `g_kill`) is not called:
   ```cpp
   g_host_away = away;
   if (!away)
       guest_tick(now);
   ```
4. While the host is away the guest plays on with its own NPCs. They can die, be eaten and be removed, and pooled creature objects get reused.
5. When the host is back in the same world (`host_here`; a world change would clear `g_kill` through `on_world_change`), `guest_tick` reads the stale entries:
   ```cpp
   for (void* c : g_kill)
       if (c != av && !game::is_puppet(c) && !game::is_destroyed(c) && !game::destroy_pending(c)) {
           game::destroy_creature(c);
   ```
   `is_destroyed` reads `+0x21` of an object that may be freed, and `DestroyInstance` runs on it, or on another object that now uses the same memory.

### Confidence
PLAUSIBLE. The code path is CONFIRMED by reading. Not verified: how many entries the flip frame usually holds (normally few, since native NPCs are removed the frame they first update) and whether a freed creature object keeps `+0x21` set in the pool, which would make the stale entries harmless.

## Fix
Not fixed yet. Suggested in the review:

Clear `g_kill` on every path of `npc::tick` that does not call `guest_tick` (the away branch, `!npc_stage()`, NPC sync off). Or let `update_filter` read `C.host_away()` directly, so it never collects while the host is away. Old behaviour behind `coop.fix kill_list_clear off`.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_host_away_kill.py` on two instances: on the guest, start a cinematic so creatures are deferred and re-collected every frame; the host goes to the main menu (`host_away=1` on the guest) and back into the same world after the guest has killed and removed those creatures. Assert that the guest game did not fault (the lab's crash check) and that a creature created in the meantime at a reused address is still alive (`+0x21` = 0, `coop.npc creatures`). The test can be made reliable with a debug command that records the pointers in `g_kill` at the flip and forces their reuse. It fails on main and passes with the fix.

### Related
- docs/re/creature.md 16 (deferred destroys during cinematics).
- The issue about pooled creature objects keeping the old net id (same pointer-reuse pattern on the host).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #60.
- 2026-10-27: ticket opened from the issue.
