---
id: BUG-0110
title: a fault inside the NPC AI tick leaves the partner's puppet as the game's avatar for good
status: open
kind: bug
stage: Creature
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#89
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #89
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/89 (full text, discussion).

Several detours swap game state around the call to the original function and put it back only when that call returns normally. The worst one is `h_NPCTickAI`: it writes the partner's puppet into `cGameNounManager::mpAvatar` (+0x54) for the length of one NPC tick. If the game faults inside that tick and its own main-thread handler swallows the fault, the stack unwinds past our clang frame and the restore code never runs. The game is then left with the puppet as its avatar, `g_swap_real` stays set and the mod's own view of the avatar goes stale.

### Where
- `h_NPCTickAI`: swap at L256-L257, restore at L268-L270 (only on normal return): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L243-L271
- `game::avatar()` returns `g_swap_real` whenever it is set: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L338-L351
- The same "swap, call, restore" pattern:
  - `h_NoAttack` writes the partner's cell index into G+0x411C (the cell avatar index): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L732-L743
  - `h_SelectUI` clears `+0xB58` bit 0x1 and sets it again afterwards: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L472-L489
  - `h_StageMsg` points `+0xE84` of puppets and proxies at a blank object: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L504-L535
  - `b_damage` sets `g_hit_by`, which attributes capture damage to a player: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L529-L541
- Nothing catches the fault on our side. SporeKit's vectored handler only logs and returns `EXCEPTION_CONTINUE_SEARCH`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/exceptions.cpp#L76-L160. The mod has no `__try` anywhere.
- The docs say the game's handler swallows main-thread faults: [docs/02-modkit.md L172](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/02-modkit.md#L172), docs/05-re-notes.md 5.5 ("Wine swallows the exception, the frame is aborted"), and the first-chance AVs in [docs/known-issues.md L85-L87](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/known-issues.md#L85-L87).

### How to reproduce
**How to reproduce / check**

- Code reading: no restore path runs if `o_NPCTickAI` does not return.
- Runtime (lab): in a creature-stage session, make the NPC AI fault while it targets a puppet. One way is a probe or a temporary devkit command that raises an AV inside the swapped call; another is an NPC whose AI reads a missing puppet field. After the fault, read `[cGameNounManager+0x54]` and compare it with the real avatar, and read `coop.status` (`ai_swaps` stops growing).
- Not verified yet:
  - which frame catches the fault (the game's own `__except` or Wine's callback wrapper);
  - whether playable frames follow;
  - whether a save written afterwards stores `mpAvatar`.

### Expected
After any return or unwind of the tick, `mpAvatar`, `g_swap_real`, the lent tuning and the cell avatar index hold their real values again.

## Root cause
**What happens**

1. `h_NPCTickAI` stores the real avatar in `g_swap_real`, writes the puppet `t` into `mpAvatar` and lends the avatar's `+0xE84` tuning to the puppet. Then it calls `o_NPCTickAI`.
2. Game code in that tick faults. This is the same code path whose crash class (0xD48634, 0xB88BEA, see docs/re/creature.md) is why the tuning loan exists. The game's handler catches the fault in an outer frame, and the unwind skips our frame.
3. Afterwards:
   - The game's `mpAvatar` is still the partner's puppet. Whatever the game does with its avatar (input, camera, AvatarTickAI, possibly a save) now uses our object. CLAUDE.md: "our objects never go into a save".
   - `g_swap_real` stays non-null. `h_NPCTickAI` only picks a target when `!g_swap_real`, so every later swap is off.
   - `game::avatar()` returns the stale `g_swap_real`. After a load or a stage change that creature is freed, so every caller of `game::avatar()` then uses freed memory.
   - The puppet keeps the avatar's tuning pointer.
4. The other detours leave smaller leftovers. `h_NoAttack` leaves the partner's cell as the avatar index of the cell game, which is as bad as above in the cell stage. The others leave a wrong flag bit, a blank `+0xE84` pointer, or a stale `g_hit_by`.

### Confidence
PLAUSIBLE, low to medium severity: a game fault inside the swapped call must happen first. The mechanism (restore only on normal return, no handler of ours) is confirmed by reading the code, and a second reviewer verified it independently. That reviewer also found the stale `game::avatar()` effect.

## Fix
Not fixed yet. Suggested in the review:

- Keep the swap state in one place and check it once per frame. If `g_swap_real` is still set outside `h_NPCTickAI`, write it back to `mpAvatar`, clear the lent tuning, clear `g_swap_real` and log it. Do the same for the saved G+0x411C in `h_NoAttack`.
- An alternative is a real SEH frame around the original call (`__try1`/`__except1` in mingw, or a small asm thunk that registers an `EXCEPTION_REGISTRATION`).
- `h_SelectUI`, `h_StageMsg` and `b_damage` can use the same per-frame check.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A test in `tools/tests/` that makes the swapped NPC tick fault once, for example through a test-only switch that dereferences null inside the detour's critical section after the swap (inside game code via a probe if possible). Then it asserts the game's own `mpAvatar` and the avatar shown to the player, not a mod counter.
- FAILS on main: `mpAvatar` stays the puppet and swaps stop.
- PASSES with the fix: the next frame restores it and swaps continue.

### Related
#69 (freed puppet after a world teardown), #44 (puppet or proxy gets its AI back during a cinematic).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #89.
- 2026-10-27: ticket opened from the issue.
