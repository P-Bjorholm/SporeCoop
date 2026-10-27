---
id: BUG-0035
title: a received world can move and overwrite the save slot the guest is playing (auto_load_world=0)
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#13
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #13
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/13 (full text, discussion).

With `auto_load_world=0`, a guest that is playing its only co-op save slot and receives the host's world again gets that live slot moved to the backup folder and then rewritten with the host's new files. This breaks the CLAUDE.md rule "never change files of the save that is loaded". Severity: data loss / wrong behaviour (the loaded game's save folder changes under it, and the feed wrongly says the player's own save was backed up).

### Where
- `pick_slot`, backup loop without the busy-slot check: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L868-L886
- The busy check exists only in the first loop: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L856-L867
- `finish()` writes every received file into the returned slot: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L998-L1024

### How to reproduce
1. Two instances (host + guest), any stage, guest with `auto_load_world=0`.
2. Host shares its world. The guest receives it into a new co-op slot (say Game1) and loads it from the galaxy menu by hand. Make sure no other `Games\GameN` holds a `sporecoop.txt` with `host_slot`.
3. While the guest plays Game1, make the host share again (`coop.share`, or the guest rejoins).
4. Look at the guest's `Games` folder: Game1 was moved to `Games-sporecoop-backup\Game1-<date>`, and a new Game1 holds the host's new files while the guest's game is still running from it.

### Expected
The slot the game is playing is never moved or written. The new world goes to another co-op slot or a free `GameN`.

## Root cause
**What happens**

1. `finish()` calls `pick_slot(crc, planet)`.
2. `busy = current_db_dir()` is the slot the save database points to, which is the slot being played.
3. Loop 1 returns the first co-op slot (`sporecoop.txt` contains `host_slot`) and skips `busy`:
   ```cpp
   if (!busy.empty() && same_dir(p, busy))
       continue;
   ```
   When the guest is playing its only co-op slot, nothing is returned here.
4. Loop 2 (only when `!g_auto_load`) has no busy check. The busy slot holds the planet (it is the same host world), so:
   ```cpp
   if (dir_has(p, widen(planet_spo))) {
       ...
       MoveFileW(p.c_str(), dst.c_str());
       g_feed("your own save of this planet was backed up to " + narrow(dst));
       return p;
   }
   ```
5. The return value does not depend on whether `MoveFileW` succeeded. On Windows a rename of a folder with open files can fail, and the files of the loaded slot are then overwritten in place. Under Wine the rename succeeds: the game's save database now points to a folder that `finish()` creates again, filled with the host's files. The game's next save (the quit message that `load_slot` posts, a pause-menu save) writes into that mixed folder.
6. The message tells the player their own save was backed up, but what was moved is the co-op slot.

### Confidence
CONFIRMED by reading the full path (`finish` -> `pick_slot` -> file writes). Not run in the harness. How the game reacts later (to the moved folder under Wine, or to the overwritten files on Windows) is not verified.

## Fix
Not fixed yet. Suggested in the review:

Skip `busy` in the backup loop too (`if (!busy.empty() && same_dir(p, busy)) continue;`). Also check the result of `MoveFileW` and do not return that slot when the move failed. Keep the old behaviour behind `coop.fix busy_slot_backup off` for the lab.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_world_busy_slot.py` on two instances, guest with `auto_load_world=0`: share, load the co-op slot by hand on the guest, share again. Assert on the guest's file system (not a mod counter) that the slot the save database points to (`coop.status` or the game's DB location) is not moved, still holds its files with unchanged modification times, and that the new world went to another `GameN`. It must fail on main (the slot is moved or rewritten) and pass with the fix.

### Related
- Same file: `pick_slot` computes `want` but never uses it (separate issue).
- CLAUDE.md section 2, "Never change files of the save that is loaded".

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #13.
- 2026-10-27: ticket opened from the issue.
