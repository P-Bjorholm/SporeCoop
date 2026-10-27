---
id: BUG-0114
title: every game start deletes all *.old files in the game folders, also the player's own backups
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#93
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #93
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/93 (full text, discussion).

`upd::cleanup_old` is meant to remove the files an update replaced last time. It deletes every file matching `*.old` in `SporebinEP1`, `SporebinEP1\mods`, `SporebinEP1\mods\sporekit\symbols` and `Data`, no matter who made it. It runs at every coop.dll start and in the installer. A player's own backup such as `SporeApp.exe.old` (a common name when patching the exe, e.g. a 4GB patch) or another mod's `.old` file is deleted silently.

### Where
- The pattern delete: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/winupdate.cpp#L339-L353
- Called at every coop.dll init: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/autoupdate.cpp#L101
- Called by the installer after install and uninstall: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L230 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L262
- `apply_pack` also deletes an existing `<file>.old` before it renames the current file, for every file in the pack: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/winupdate.cpp#L252-L255

### How to reproduce
1. In the game folder, copy `SporebinEP1\SporeApp.exe` to `SporebinEP1\SporeApp.exe.old` (or put any `x.old` into `Data`).
2. Start Spore with SporeCoop installed (or run the installer's Install / Remove).
3. The `.old` file is gone.

### Expected
Only files that SporeCoop's updater renamed itself are deleted.

## Root cause
**What happens**

- `FindFirstFileW(dir + "\\*.old")` in four folders, `DeleteFileW` on every match. No list of what the updater itself renamed is kept.
- So on every game start with SporeCoop, a file like `SporebinEP1\SporeApp.exe.old`, `Data\<something>.package.old` or `mods\<othermod>.ini.old` is removed without a message.
- `apply_pack` does the same for the names of the pack's files (`steam_api.dll.old`, `sporekit.dll.old`, `mods\coop.dll.old`, `Data\SporeCoop.package.old`, the `.sym` files). That is a smaller risk, since those names belong to SporeCoop, but a player's own `steam_api.dll.old` backup is lost there too.

### Confidence
CONFIRMED by reading the code. A second reviewer checked it independently. Severity low (data loss of files the player made, in a narrow but real case).

## Fix
Not fixed yet. Suggested in the review:

- Record the files `apply_pack` renamed to `.old` (e.g. in `mods\sporecoop.cleanup`, next to `sporecoop.pending`), and have `cleanup_old` delete only those, keeping entries whose delete fails (still loaded) for the next start.
- Alternatively, use a name only SporeCoop makes, such as `<file>.sporecoop-old`, and delete only that pattern.
- In `apply_pack`, do not delete an existing `<target>.old` that SporeCoop did not make: move it aside or use the unique suffix.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A host-side test of the updater (like the existing `update_check.py` / net_tests for `pack.cpp`), with the old behaviour behind a switch:
- Put `SporebinEP1\SporeApp.exe.old` and `Data\keep.old` into a test game folder, apply a pack, then call `cleanup_old`.
- Assert both files still exist and the updater's own `.old` files are gone.
- FAILS on main (both player files deleted). PASSES with the fix.

### Related
- #67 (a pack listing the same file twice loses the original file on rollback)
- #58 (install.bat overwrites the player's coop.ini on a reinstall)

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #93.
- 2026-10-27: ticket opened from the issue.
