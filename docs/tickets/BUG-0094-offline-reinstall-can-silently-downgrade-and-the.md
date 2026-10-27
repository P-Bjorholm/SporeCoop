---
id: BUG-0094
title: offline "Reinstall" can silently downgrade, and the pack used is picked arbitrarily
status: open
kind: bug
stage: Installer
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#72
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #72
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/72 (full text, discussion).

When GitHub cannot be reached, SporeCoopSetup.exe installs the update pack found next to it without comparing its version with the SporeCoop already installed. A player with a newer SporeCoop who runs an older setup (for example from an old download folder) gets the older version installed, under a button labelled "Reinstall" and a message saying it is installed. If several `SporeCoop-*.pack` files lie next to the setup, the one used is simply the first the file system lists, which is not necessarily the newest (NTFS lists by name, so `SporeCoop-0.10.0.pack` comes before `SporeCoop-0.11.0.pack`). Severity: wrong behaviour (players end up on different versions, which co-op requires to match).

### Where
* `local_pack()`: the first `FindFirstFileW` match: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L182-L196
* `do_install()`: which pack is used, no comparison with the installed version: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L198-L233
* the button label, computed from the GitHub release only: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L295-L302
* `installed_version()`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/winupdate.cpp#L355-L369

### How to reproduce
1. Install SporeCoop 0.8.x into a game folder (`SporebinEP1/mods/sporecoop.version` says 0.8.x).
2. Put an older `SporeCoopSetup.exe` with its `SporeCoop-0.7.x.pack` into a folder, disconnect from the network (or pass `/api:` an address that does not answer).
3. Run it, click "Reinstall" (or run `SporeCoopSetup.exe /S /dir:<game folder> /api:http://127.0.0.1:9/`).
4. Observed by reading: 0.7.x is installed, exit code 0, `sporecoop.version` now says 0.7.x.
5. For the choice: put `SporeCoop-0.9.0.pack`, `SporeCoop-0.10.0.pack` and `SporeCoop-0.11.0.pack` next to the setup and stay offline. By name (`LC_ALL=C sort` gives the same order as NTFS here) `SporeCoop-0.10.0.pack` is listed first, so 0.10.0 is installed although 0.11.0 is the newest.

### Expected
* Among several local packs the setup uses the one with the highest version (`upd::compare_versions`), or asks.
* It never installs a version older than the installed one without saying so: the button reads "Install older version 0.7.x" and asks for confirmation; silent mode refuses with an error unless an explicit flag (for example `/downgrade`) is given.

## Root cause
**What happens**

1. `local_pack()` calls `FindFirstFileW(dir + "\\SporeCoop-*.pack")`, takes the first result and closes the search. The order is whatever the file system returns (alphabetical on NTFS, unspecified elsewhere), so the choice depends on the file names, not on the versions in them.
2. `do_install()` reads that pack. It goes to GitHub only when there is no usable local pack, or when the release check has succeeded and GitHub's version is newer:
   ```cpp
   bool have_local = !lp.empty() && upd::read_pack_file(lp, pack, err);
   if (!have_local || (g_rel_ok && upd::compare_versions(g_rel.version, pack.version) > 0)) {
   ```
3. Offline (`g_rel_ok` false) the local pack is installed whatever its version. `upd::installed_version(root)` is never consulted in `do_install`, so nothing stops installing 0.7.x over 0.8.x.
4. The button text comes from `v` (installed) and `g_rel` (GitHub): offline it shows "Reinstall" although a different, older version is about to be installed. Afterwards the status says "SporeCoop 0.7.x is installed", which is the only hint.
5. The in-game updater has a downgrade guard (mods/coop/autoupdate.cpp refuses a pack that is not newer); the setup has none.

### Confidence
CONFIRMED by reading the full path (`local_pack` -> `do_install` -> `apply_pack`, no version check against the installed one). Not run on Windows.

## Fix
Not fixed yet. Suggested in the review:

* Move the choice into portable code: `std::string upd::pick_pack(const std::vector<std::string>& names)` in updater/pack.cpp, returning the name with the highest version parsed from `SporeCoop-<version>.pack`; `local_pack()` enumerates all matches and uses it.
* In `do_install`, compare `pack.version` with `upd::installed_version(root)`: when lower, return a clear message in silent mode (unless `/downgrade`), and in the window show it in the button text and ask before installing.
* Compute the button label from the pack that will actually be installed.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

* Host test in tests/net_tests.cpp for the new helper: `CHECK(upd::pick_pack({"SporeCoop-0.10.0.pack", "SporeCoop-0.11.0.pack", "SporeCoop-0.9.0.pack"}) == "SporeCoop-0.11.0.pack")`. Implemented the way `local_pack()` works today (first name in listing order) it returns `SporeCoop-0.10.0.pack` (FAIL); with the fix it returns the newest (PASS).
* In `tools/tests/update_check.py`: install the current pack into the lane, then run the setup silently with `/pack:` an older pack built by `tools/mkpack.py` with a lower version and `/api:` an address that does not answer; assert a non-zero exit code and that `sporecoop.version` still holds the newer version. On main the setup exits 0 and the version file shows the older version (FAIL); with the fix it refuses (PASS).

### Related
The in-game updater's downgrade guard in mods/coop/autoupdate.cpp (`the release pack is not newer`).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #72.
- 2026-10-27: ticket opened from the issue.
