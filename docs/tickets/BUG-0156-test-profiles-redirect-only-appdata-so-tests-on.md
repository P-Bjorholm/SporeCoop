---
id: BUG-0156
title: test profiles redirect only AppData, so tests on lane 1 can write into the owner's own Documents (My Spore Creations)
status: open
kind: test
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#135
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #135
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/135 (full text, discussion).

`-sporekit-profile:<name>` (and the implied `inst<N>` profile) redirects the game's user data by hooking `SHGetFolderPathW` for `CSIDL_APPDATA` only. Spore also writes outside AppData: the "My Spore Creations" folder in the user's Documents (PNG cards of saved creations, screenshots). Lane 1 is the real install in the owner's own "Games" bottle, so editor tests there can write into the owner's own Documents. CLAUDE.md: "Tests must not touch the player's own Spore profile (only the test profiles)".

### Where
- The profile hook, `CSIDL_APPDATA` only: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/instance.cpp#L22-L32
- Only `SHGetFolderPathW` is hooked (no A variant, no `SHGetSpecialFolderPathW`, no known-folder API): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/instance.cpp#L48-L61
- Lane 1 is the owner's real install and bottle: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/spore.sh#L6-L18
- The lab uses lane 1: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L143

### How to reproduce
1. Note the contents of Documents\My Spore Creations in the "Games" bottle.
2. Run an editor test on lane 1 (`tools/lab.py run coop_guest_editor --lanes 1`), which saves a creation in a test profile.
3. Check whether new PNGs appeared in the owner's Documents\My Spore Creations (and whether test profile folders `AppData\Roaming\SporeKit-*` hold a Documents equivalent: they do not).

### Expected
A test profile redirects every per-user folder the game writes to (AppData and Documents at least), so tests never add or change files in the owner's own folders.

## Root cause
**What happens**

- With a profile, `SHGetFolderPathW(CSIDL_APPDATA)` gets `\SporeKit-<name>` appended; every other folder id is passed through unchanged.
- Spore keeps "My Spore Creations" (creation PNGs, screenshots) under the Documents folder. Tests that open and save in editors (`coop_editor_*`, `coop_guest_editor`, `coop_vote`, the creations tests) or take game screenshots on lane 1 therefore write into the Documents folder of the owner's own bottle, next to the owner's own creations.
- Lane bottles N>1 are copies of the "Games" bottle; if Documents in that bottle is a link to the home folder (Wine's default desktop integration), all lanes write there.

### Confidence
PLAUSIBLE, severity low. CONFIRMED by reading that only `CSIDL_APPDATA` is redirected. That Spore writes into Documents through `SHGetFolderPathW(CSIDL_PERSONAL)` (and not another API) is not verified in this game build. A second reviewer verified the reading.

## Fix
Not fixed yet. Suggested in the review:

- In `h_SHGetFolderPathW`, also redirect `CSIDL_PERSONAL` / `CSIDL_MYDOCUMENTS` (to e.g. `AppData\Roaming\SporeKit-<name>\Documents`), and `CSIDL_LOCAL_APPDATA` if the game uses it.
- Check with the `probe` file trace (CreateFileW paths containing "My Spore") which API the game uses, and hook `SHGetFolderPathA`, `SHGetSpecialFolderPathW` or `SHGetKnownFolderPath` too if needed. Record the result in docs/06-testing.md.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A lane test with a profile:
- Before: list the owner's Documents\My Spore Creations (or a marker folder in the bottle). Run an editor save in a test profile (`coop.vote editor` path or the creations test).
- Assert: no new or changed file in the owner's Documents; the PNG is in the profile's redirected folder.
- FAILS on main (the PNG lands in the owner's Documents), PASSES with the fix. Uses the CreateFileW trace as the check, not a mod counter.

### Related
- #80 (spore.sh fixture restore with profile "-" deletes the player's own saves)
- #81 (more places that can kill the owner's own Spore on lane 1)

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #135.
- 2026-10-27: ticket opened from the issue.
