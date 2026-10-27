---
id: BUG-0055
title: after leaving a session, the host's creations still replace a guest's own creation with the same id
status: open
kind: bug
stage: Creature
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#33
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #33
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/33 (full text, discussion).

On a guest, each creation the host shares is registered under the host's original key at priority 2000, above the guest's own My Creations (1000). Nothing unregisters these packages when the session ends. Creation ids are per profile and sequential, so a guest often owns a different creation with the same id; for the rest of the game process that key still resolves to the host's records, also in the guest's own solo games. Severity: wrong behaviour (the guest's own creature or cell shows as the host's after leaving).

### Where
- Registration under the original key for the host's (owner 0) bundles: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L430-L446
- `g_world_pkgs`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L362-L368
- `on_session_end` leaves them registered: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L991-L1006
- A user leave (`do_leave`) does not call `creations::on_session_end` at all, only the `disconnected` callback does: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1760-L1770 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1490-L1501

### How to reproduce
1. Two test profiles whose first own creature has the same instance id (creation ids are sequential per profile; the `nest_species` fixture has such a pair).
2. Host and guest in the Creature stage, the host playing its own creature. The guest joins and gets the host's creation bundle.
3. The guest leaves the session (menu Leave, or the host quits) and, without restarting Spore, loads its own save that uses its own creature of that id.
4. The guest's avatar and the Sporepedia entry show the host's creature.

### Expected
After the session ends, the guest's own creations resolve to its own records again; the host's creations stay only under remapped keys (0x7C... / 0x7D...) or are unregistered.

## Root cause
**What happens**

1. The host's own creation bundle (owner 0) reaches the guest. `apply()` writes `world_<kind>_<id>.package` with the records unchanged (the host's key) and registers it at `kPrioWorld` (2000):
   ```cpp
   if (owner == 0 && C.sess && C.sess->role() == net::Role::Client) {
       WorldPkg& wp = g_world_pkgs[{(uint8_t)k, b.key.instance}];
       ...
       if (void* wdb = package_for(path, b.recs, kPrioWorld)) {
   ```
   This happens for every kind (creature, cell, ufo), and there is no check whether the guest already has a creation under that key.
2. During the session this is intended (the host's world names its creatures by that key; `own_key_here` remaps the guest's own avatar when the keys collide).
3. `on_session_end` clears `g_applied`, `g_remap` and `g_unmap`, but not `g_world_pkgs`, and it does not unregister those packages ("registered packages stay"). After the session, nothing remaps the guest's own key any more.
4. The resource manager searches the highest priority first (docs/re/creations.md section 3), so a guest's own creation with the same instance now resolves to the host's bem and metadata: in its own saved game, its Sporepedia and its editor. The guest's own cached `creaturedata` in Pollination.package is still its own (docs/re/creature.md#15), so the game can also mix the two creations.
5. With "Leave" from the menu (`do_leave`), even `creations::on_session_end` is skipped.

### Confidence
CONFIRMED by reading the full path (registration, `on_session_end`, `do_leave`). PLAUSIBLE effect: how the game mixes the host's bem with the guest's own cached creaturedata after leaving is not verified at runtime.

## Fix
Not fixed yet. Suggested in the review:

In `on_session_end`, unregister every package in `g_world_pkgs` (`drop_package`) and clear the map. Call `creations::on_session_end` from `do_leave` and `on_unload` too, like the `disconnected` callback does. Keep the `species_` packages (registered only for keys the guest did not have). Old behaviour behind `coop.fix world_pkgs_drop off`.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_creation_leave.py`: two instances with colliding creation ids (`nest_species` profiles). The guest joins, receives the host's creation and leaves. Then the guest loads its own save and runs `coop.creations bem <instance>` (the body the game finds for that key). Assert that the bem hash equals the guest's own EditorSaves record, not the host's. It fails on main and passes with the fix.

### Related
- docs/re/creature.md 15 (another creation under the same key), docs/re/creations.md (priority 2000 packages).
- The same "state not reset at session end" pattern: the issue about `g_deferred` / `g_via_menu` in world.cpp.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #33.
- 2026-10-27: ticket opened from the issue.
