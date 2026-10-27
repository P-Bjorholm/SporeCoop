---
id: BUG-0142
title: a Cyrillic or CJK player name becomes "????" after a restart, and a name typed into coop.ini by hand is garbled
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#121
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #121
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/121 (full text, discussion).

The player name (and the other co-op settings) is stored in `SporebinEP1\mods\coop.ini` through `WritePrivateProfileStringW` / `GetPrivateProfileStringW`. The shipped `coop.ini` is a plain ASCII file without a byte order mark. For such a file, Windows (and Wine) write and read in the system ANSI codepage, not in Unicode. A name typed in the Co-op window with characters outside that codepage is saved as `?` characters. A name typed by hand into `coop.ini` with a modern editor (UTF-8) is read back garbled. The co-op window offers Russian as a UI language, so Cyrillic names are expected.

### Where
- Name read at start: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2632-L2634
- Name saved from the Co-op window: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L267-L286
- SporeKit's ini read and write (UTF-8 to UTF-16, then the profile API): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/main.cpp#L17-L28 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/main.cpp#L85-L99
- The shipped file (ASCII, no BOM), which invites editing the name by hand: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/data/config/coop.ini#L1-L4

### How to reproduce
1. On a Western-locale Windows or Wine prefix, open the Co-op window, enter the name "Вася", and host or join (the name is saved then).
2. Quit and restart Spore. The Co-op window and the partner show "????". `coop.ini` contains `name=????`.
3. Alternatively, edit `coop.ini` in Notepad to `name=Jörg`, save (UTF-8), start: the name shows as "JÃ¶rg".

### Expected
Any name the Co-op window accepts survives a restart unchanged, and a UTF-8 `coop.ini` edited by hand is read correctly.

## Root cause
**What happens**

- `WritePrivateProfileStringW` writes Unicode only into a file that already is UTF-16 (with a BOM); otherwise it converts to the ANSI codepage (documented behaviour; Wine does the same). On a system with codepage 1252 (Western Windows, and Wine with an English or German locale), "Вася" or "太郎" are stored as `????` / `??`. In the current session the name is still right (it is kept in memory), so the player only notices after the next start, when the partners see "????".
- Reading: `GetPrivateProfileStringW` on a file without a BOM decodes it with the ANSI codepage. Notepad on Windows 10/11 and every Linux editor save UTF-8 without a BOM, so `name=Jörg` typed by hand becomes "JÃ¶rg", and that is what is sent to the other players and shown over the puppet.
- Players whose system codepage matches their script (e.g. Russian Windows, codepage 1251) do not see it; players with a name outside their codepage do. Any other text value written this way is affected too.

### Confidence
CONFIRMED by reading the code and the documented behaviour of the profile API. A second reviewer verified it. Severity low, but visible to players.

## Fix
Not fixed yet. Suggested in the review:

- Before the first write, if `coop.ini` has no UTF-16 BOM, rewrite it as UTF-16LE with a BOM (converting the existing content from UTF-8 when it decodes as valid UTF-8, else from the ANSI codepage). After that the profile API reads and writes Unicode.
- Or replace the profile API in SporeKit's `config_*` with a small UTF-8 ini reader/writer of our own (and write atomically: temp file plus rename).
- Ship `data/config/coop.ini` in the chosen encoding.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A test on two instances (the name must go through the Co-op window's name field; the `coop.name` console command does not save to coop.ini):
- Enter "Вася 太郎" in the Co-op window's name field and host, restart the host game, join with the other instance, and read the name the guest sees (its `coop.status`, the caption over the puppet).
- A second case: write `name=Jörg` as UTF-8 into the lane's `coop.ini`, start, check the partner sees "Jörg".
- FAILS on main ("????" / "JÃ¶rg"). PASSES with the fix.

### Related
- #58 (install.bat overwrites the player's coop.ini on a reinstall)

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #121.
- 2026-10-27: ticket opened from the issue.
