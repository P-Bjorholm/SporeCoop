---
id: BUG-0116
title: the required fix switch `coop.fix NAME off` does not exist, and several 0.7.2.x fixes have no switch
status: open
kind: test
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#95
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #95
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/95 (full text, discussion).

The project rules, the PR template, the fix-player-report skill and the ticket template all tell contributors to keep the old behaviour behind `coop.fix <name> off`, so the lab can later show a fix's test failing again. No such console command exists. Each fix has its own ad-hoc switch, if it has one at all:
- `coop.pool mirrorfix 0|1`
- `coop.npc cinedefer|strays|hatch|partners on|off`

Several fixes from 0.7.2.2-0.7.2.4 have no switch at all, so "the test fails without the fix" cannot be re-checked on a current build.

### Where
- The rule: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/CLAUDE.md#L90-L93
- PR template: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/.github/PULL_REQUEST_TEMPLATE.md#L11
- Skill step 5: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/.claude/skills/fix-player-report/SKILL.md#L23-L25
- Ticket template: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tickets.py#L74
- The switches that do exist:
  - https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2089-L2107
  - https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/cellsync.cpp#L1786-L1788
- Command registration (no `coop.fix`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2713

### How to reproduce
**How to reproduce / check**

1. In the cheat console: `coop.fix mirrorfix off` gives an unknown command.
2. `grep -rn "coop.fix" .` matches only CLAUDE.md, the PR template, the skill and tickets.py.

### Expected
One documented switch command (`coop.fix` with no arguments listing all fixes and their state; `coop.fix <name> on|off`), on by default in release. Every fix where it is possible registers there. The docs and templates name commands that exist.

## Root cause
**What happens**

- The claim: CLAUDE.md says "A fix keeps the old behaviour behind a switch (`coop.fix <name> off`) where it can, so the lab can check later that the test still catches the bug." The PR template asks for "FAILS without the fix: ... (main @ <sha>, or `coop.fix <name> off`)".
- The reality: `grep -rn 'coop\.fix' mods sporekit` finds nothing; the string appears only in the four docs above. The existing switches each have their own syntax and are undocumented except in code comments and single tests.
- 0.7.2.x fixes and their switches:

  | Ticket | Fix | Switch |
  |---|---|---|
  | BUG-0001 | cell spin and bites | `coop.pool mirrorfix 0` |
  | BUG-0018 | migration crash | `coop.npc cinedefer off` |
  | BUG-0008 | strays | `coop.npc strays off` |
  | BUG-0014 | hatchling | `coop.npc hatch off` (unused by its test) |
  | BUG-0016 | NPCs target partners | `coop.npc partners off` |
  | BUG-0012 | Alt+Shift+Enter guard, gamefixes.cpp | **none** (its ticket says "FAIL with the hook off", which needs a rebuild) |
  | BUG-0006 | cell_leave forget | **none** |
  | BUG-0009 / BUG-0010 | creature_desync: stale proxies, species request | **none** |
  | BUG-0015 | play_death on mirrors | **none** |
  | BUG-0017 | nest_species | **none** |
  | BUG-0002 | cell meat | **none** |
  | BUG-0003 | join new game | **none** |

### Confidence
Confirmed by reading (grep over the whole tree). A second reviewer verified it.

## Fix
Not fixed yet. Suggested in the review:

- Add `coop.fix` in coop.cpp over a small registry `{name, bool*}`. Register the existing flags: `mirrorfix`, `cinedefer`, `strays`, `hatch`, `partners`.
- Add switches for the fixes that lack one (at least the alt-enter guard and play_death).
- Keep the old commands as aliases.
- Have `tools/tickets.py check` warn when a fixed ticket names no switch.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A test runs `coop.fix` and checks that every listed fix is `on` by default. Each fixed ticket's test gets a `--without-fix` mode using `coop.fix <name> off`, and it must FAIL there (as coop_cell_spin.py and coop_migration.py already do).

### Related
#20, #51 (tests that pass without proving the fix).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #95.
- 2026-10-27: ticket opened from the issue.
