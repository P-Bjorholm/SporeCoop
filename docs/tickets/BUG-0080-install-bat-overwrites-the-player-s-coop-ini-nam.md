---
id: BUG-0080
title: install.bat overwrites the player's coop.ini (name, update opt-out) on a reinstall
status: open
kind: bug
stage: Installer
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#58
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #58
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/58 (full text, discussion).

The documented manual update ("extract the new zip into `SporebinEP1` over the old files and run `install.bat` again") replaces the player's `mods\coop.ini` and `mods\sporekit.ini` with the defaults. That loses the player name, the last joined address, the host port, and the `[update] check=0` opt-out from the update check. A player who turned the GitHub check off gets it turned back on without being told. The in-game update and `spore.sh deploy` keep these files; only the zip path resets them.

### Where
- `install.bat` (written by `package.sh`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/package.sh#L140-L141
- The zip ships the default inis inside `SporeCoop\mods`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/package.sh#L112
- The update pack keeps them on purpose (`ONLY_IF_MISSING`, "the player's settings stay"): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/mkpack.py#L25-L26
- `spore.sh deploy` also copies an ini only when missing: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/spore.sh#L74-L77
- Settings the mod writes into `coop.ini`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L267-L319 and the opt-out read in https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/autoupdate.cpp#L97
- The documented paths: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/INSTALL.md#L216-L224

### How to reproduce
1. Install a release zip with `install.bat`, start the game, set a name in the Co-op window (or add `[update]` / `check=0` to `mods\coop.ini` as INSTALL.md says).
2. Extract the same or a newer zip over `SporebinEP1` and run `install.bat` again.
3. `mods\coop.ini` is the default file again: `name=Player`, no `[update]` section; the next start asks GitHub for updates.

(Read from the script; not run on Windows.)

### Expected
A reinstall from the zip keeps existing `.ini` files, like the in-game update and the setup do.

## Root cause
**What happens**

```bat
if not exist mods mkdir mods
xcopy /e /i /y /q SporeCoop\mods mods >nul
```

`/y` overwrites existing files without asking, so `SporeCoop\mods\coop.ini` (defaults: `name=Player`, no `[update]` section) replaces the player's file. The UI saves `coop/name`, `ui/last_address` and `ui/host_port` into that same file (`save_str` -> `config_set_str` -> `WritePrivateProfileStringW` on `mods\coop.ini`), and INSTALL.md section 8 tells players to put `check=0` there.

### Confidence
CONFIRMED by reading.

## Fix
Not fixed yet. Suggested in the review:

Copy everything but the inis with `xcopy`, then the inis only when missing:

```bat
xcopy /e /i /y /q /exclude:SporeCoop\ini.exclude SporeCoop\mods mods >nul
for %%f in (SporeCoop\mods\*.ini) do if not exist "mods\%%~nxf" copy /y "%%f" "mods\%%~nxf" >nul
```

(with `ini.exclude` containing `.ini`), or move the default inis out of `SporeCoop\mods` into `SporeCoop\defaults` in the zip so `xcopy` never sees them.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A Wine (or Windows) check: stage the zip with `tools/package.sh`, run `install.bat` in a fake `SporebinEP1` (an empty `SporeApp.exe` file is enough for its existence check), change `mods\coop.ini`, run `install.bat` again: the change must survive. It is lost today.

### Related
`tools/mkpack.py` comment "the player's settings stay"; docs/INSTALL.md section 8.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #58.
- 2026-10-27: ticket opened from the issue.
