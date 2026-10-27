---
id: BUG-0148
title: coop_creature_desync's "killed NPC is dead on the guest" reads the mod's flag, not the game's death
status: open
kind: test
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#127
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #127
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/127 (full text, discussion).

`coop_creature_desync.py` (suite `creature_desync`, the test of BUG-0009 and BUG-0010) checks after each editor and reload that "an NPC the host kills dies on the guest". On the guest it reads " DEAD" from `coop.npc list`. On a guest, that word is the mod's own `px.killed` flag, not the game's death flag of the mirror. That is exactly the failure mode of BUG-0015: the mod marks the proxy killed while the game's creature stands at 0 health, alive. The check passes in that case.

Two things limit the impact:
- The BUG-0009 regression itself is still caught by this test's "ghost" check (line 98).
- `coop_creature_kills.py` asserts the game's dead flag for a kill by the guest.

But no test asserts the game's death on the guest for a kill **by the host** after an editor or a reload, which is what this step claims to check.

### Where
- The kill check: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_creature_desync.py#L100-L109
- The parser that maps " DEAD" to 'dead': https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_creature_desync.py#L48-L58
- Guest `coop.npc list`, " DEAD" from `px.killed`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L885-L897
- Host `coop.npc list`, " DEAD" from the game's `is_dead`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L874-L879
- Where `px.killed` is set before the game's death is played: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/npc.cpp#L468-L478
- The project's own note that this " DEAD" is bookkeeping: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/06-testing.md#L746-L752
- The right way, in the sibling test (`game_dead`, `coop.npc creatures`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tests/coop_creature_kills.py#L48-L54

### How to reproduce
**How to reproduce / check**

Comment out `game::play_death(px.c);` in npc.cpp (the BUG-0015 fix). `coop_creature_desync.py` still passes its "killed ... is dead on the guest" checks, while `coop_creature_kills.py` fails.

### Expected
The check asserts the game's death flag (+0xB5E, `game::is_dead`) of the guest's mirror. Alternatively, `coop.npc list` on the guest prints the game's flag under another name, so the two meanings cannot be confused.

## Root cause
**What happens**

```python
host.call(f'coop.npc hosthit {nid} 100000')
dead = wait(lambda: npcs(guest).get(nid, ('', ''))[1] == 'dead', 10, 0.5)
ck(dead, f'{tag}: n{nid} the host killed is dead on the guest ({npcs(guest).get(nid)})')
```

On the guest, `npc::list()` prints `px.killed ? " DEAD" : ""`. `px.killed = true` is set as soon as the host's state carries `NF_DEAD`, before and independently of `game::apply_damage` / `game::play_death`. If `play_death` stops working, the mirror is not dead in the game, and this check still passes. The same field means two different things on host and guest (the game's flag and the mod's flag), and docs/06-testing.md 6.22 warns about it: "not our "DEAD" of `coop.npc list`, which is only our bookkeeping".

### Confidence
Confirmed by reading. A second reviewer verified it, and noted that BUG-0009 is still caught by the ghost check at line 98.

## Fix
Not fixed yet. Suggested in the review:

- In `checks()`, after the host's kill, read the mirror's pointer (`ptr=` in the guest's `coop.npc list`) and use `coop.npc creatures <species>`, as `coop_creature_kills.game_dead()` does.
- In npc.cpp, print `killed` for the mod flag and keep " DEAD" for the game's flag on both roles.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

With `play_death` disabled the changed check must FAIL. With the fix it must pass after the editors and after the reload.

### Related
#20 (suite tests that check only mod bookkeeping).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #127.
- 2026-10-27: ticket opened from the issue.
