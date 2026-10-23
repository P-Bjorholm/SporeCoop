# SporeCoop - rules for everyone working here (people, Claude sessions, subagents, test bots)

Co-op mod for Spore (GOG 2016 build, `SporeApp.exe` 24 888 320 bytes, and the GOG 2024 build,
24 895 536 bytes, see docs/05-re-notes.md 5.1.1) on the SporeKit framework, run under Wine/Bottles
on Linux and on Windows. Overview: README.md, docs/01-architecture.md … docs/10-dev-pipeline.md.

## 1. Knowledge first: write down what you learn

The project lives on reverse-engineered knowledge. Not knowing costs hours; looking it up costs
seconds. **Before analysing anything in the game, search what is already known; after learning
anything new, write it down in the same change.**

* Where knowledge lives:
  * `data/symbols/overrides_922af7c7.sym`: every game function / global / vtable we use:
    `Namespace::Name=0xADDRESS ; <confidence> <calling convention + signature + what it does>`.
    Confidence is one of `verified` (prologue / `ret N` / call sites checked, or seen working at
    runtime), `likely` (role sure, details inferred), `unknown`. `tools/re_lint.py` (in `make test`)
    fails when the code asks for a symbol that is not there or an entry has no confidence word.
    A new symbol goes into `overrides_dc04aee5.sym` (GOG 2024 build) too, with that build's
    address: it is usually not the GOG 2016 address plus the neighbours' shift (05-re-notes 5.1.1).
  * `docs/re/<area>.md` (cell, tribe, civ, space, creations, …) - structures, field offsets, how a
    game system works, recipes ("to spawn an epic: …"), with ✔ / ~ / ? marks. One file per game
    area; append a section rather than starting a new file.
  * `docs/05-re-notes.md`: method and tools of the reverse engineering.
  * `docs/known-issues.md`: bugs found and not fixed yet (with how to reproduce them).
  * `docs/06-testing.md`: test tools, fixtures, how long things take, test pitfalls.
* Code never uses a raw game address: ask SporeKit by name (`H->sym("…")`) and add the name to the
  `.sym` file.
* Agents and test bots doing analysis or exploration: put the findings into these files (or into
  your report so the caller can), including negative results ("X is not called in tribe mode",
  "field +0x84 is not the state").
* Findings at runtime count: a crash signature, a function that needs an argument nobody knew, a
  timing ("a stage switch in game takes 2 s") - write it down where the next person will look.

## 2. Checking before calling game code

* Check the signature before calling: `ret N` gives the stack bytes (thiscall: arguments only),
  vtable slots by the call sites. A missing argument corrupts the stack and crashes far away
  (citizen vt 0x80 takes an int; TakeDamage needs a non-null direction vector).
* UI, fonts, editors, saves and anything that renders: outside the frame
  (`SkHost::run_outside_frame`, commands with `SK_CMD_OUTSIDE_FRAME`), never inside Present.
* Never change files of the save that is loaded (the game reads them later): new saves go into a
  new slot.

## 3. Tests

* Every feature gets an automated test on two game instances (tools/tests/coop_*.py on top of
  `twoinst.start`), every fixed bug a test that reproduced it.
* Run tests through the lab: `tools/lab.py run smoke --lanes 3` (before a merge), `full` (big
  changes), single tests by name. Lanes are independent copies of the game + bottle
  (`tools/spore.sh lane create N`); `SPORE_LANE=N` selects one for manual runs.
* The dev loop without restarting the games: `tools/lab.py reload` (build + hot reload of coop.dll).
* Tests must not touch the player's own Spore profile (only the test profiles), run muted and invisible
  (a headless GNOME Shell, tools/headless.sh; see docs/06-testing.md).
* Pitfalls: `pkill -f <pattern>` from a shell whose own command line contains the pattern kills
  that shell; quitting one Wine instance kills every instance in the same bottle (lanes have own
  bottles); a deadlocked Wine process can keep its port - `spore.sh kill` SIGKILLs it.

## 4. Git

* `main` always works: only merged branches that passed `lab.py run smoke` (and `make test`).
* Work on `feat/<name>` or `fix/<name>`, one topic per branch; parallel work in git worktrees
  (`../SporeCoop-wt/<branch>`, `lab.py run … --branch <name>` creates and tests them).
* Small commits after each logical step, English messages and comments; a pull request per branch
  with the lab summary (`out/lab/<run>/summary.md`).
* Everything published (README, docs, release notes) is in English.
* No em dash (U+2014) in any text: code comments, commits, docs, README, release notes, UI. Use a
  colon, comma, parentheses, a period or a plain hyphen.
* Merge locally (`git merge --ff-only` or a local merge commit) and push `main`; do not use GitHub's
  merge button: it writes the account's e-mail into the merge commit. Commits use the GitHub
  no-reply address.
* Releases: add the version's section to `CHANGELOG.md`, bump `COOP_VERSION` (mods/coop/coop.cpp),
  push a tag `vX.Y.Z`: GitHub Actions builds the zip and publishes the release.
