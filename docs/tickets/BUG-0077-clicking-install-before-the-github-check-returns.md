---
id: BUG-0077
title: clicking Install before the GitHub check returns races on the release data
status: open
kind: bug
stage: Installer
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#55
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #55
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/55 (full text, discussion).

SporeCoopSetup.exe starts a background check of the newest GitHub release when its window opens. If the player clicks Install (or Update / Reinstall) before that check has finished, the install thread fetches the release itself and writes the same global `g_rel` (strings) while the check thread may assign it too, and while the UI thread reads it in `refresh()`. Concurrent writes to a `std::string` without a lock are undefined behaviour and can corrupt the heap: the setup can crash or show garbage. Severity: minor (narrow timing window, possible crash).

### Where
* the check thread, writes `g_rel`, `g_rel_ok`, `g_rel_err` without a lock: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L307-L318
* started from `WM_CREATE`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L394-L395
* the install thread, `upd::latest_release(g_opt.api, g_rel, err)` when `g_rel_ok` is still false: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L198-L225
* the UI thread reading `g_rel.version` (on `WM_INFO`, on every keystroke in the folder box, on `WM_DONE`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L275-L305
* `start_job` starting the install thread: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/installer/setup.cpp#L335-L346

### How to reproduce
Timing-dependent, not reproduced yet:

1. Make the release check slow: run `SporeCoopSetup.exe /api:http://127.0.0.1:<port>/latest` against a small local server that answers the same JSON after a delay of about 3 s (or use a throttled network).
2. Click Install within that delay; the install thread fetches the release at the same moment as the check thread.
3. Under a debugger with page heap (or a build with the address sanitizer for mingw, where available) the race shows as a heap error; without one it may crash rarely.

### Expected
The release data is read and written under one lock, or the install job never touches the shared copy; clicking Install at any moment is safe.

## Root cause
**What happens**

1. The window opens; `check_thread` starts `upd::latest_release(g_opt.api, r, err)` (an HTTPS call, from a few hundred ms up to the WinHTTP timeouts of 10 s + 10 s + 15 s + 30 s on a slow network).
2. The player clicks Install. `start_job` starts `job_thread` -> `do_install`. No local pack is used (or it is older), and `g_rel_ok` is still false, so `do_install` calls `upd::latest_release(g_opt.api, g_rel, err)`, which ends in `parse_release(json, g_rel)`: `out = Release{}` and then assignments to `tag`, `version`, `page_url`, `pack_url`, `sums_url`, `setup_url`.
3. Meanwhile `check_thread` finishes and runs `g_rel = r; g_rel_ok = ok; g_rel_err = ...;` and posts `WM_INFO`, whose handler calls `refresh()` on the UI thread, which builds strings from `g_rel.version` and calls `compare_versions(g_rel.version, v)`.
4. Two threads assigning the same `std::string` members, or one assigning while another copies, can free a buffer the other one is still using: heap corruption, a crash in the setup, or a wrong version shown. `do_install` then also uses `g_rel` for `download_pack(g_rel, ...)` while `check_thread` may still overwrite it.
5. `g_rel_ok` is a plain `bool` and `g_busy` a `volatile bool`: no ordering guarantee either.

### Confidence
CONFIRMED by reading the full path (both threads write `g_rel` without synchronisation, the UI thread reads it). PLAUSIBLE that it crashes in practice: the window is the time the release check needs.

## Fix
Not fixed yet. Suggested in the review:

* Give `do_install` its own `upd::Release rel` (copied from `g_rel` under a mutex when `g_rel_ok`, else fetched into the local copy), and pass `rel` to `download_pack`.
* Protect `g_rel`, `g_rel_ok` and `g_rel_err` with a `std::mutex` (or a critical section) in `check_thread` and `refresh()`; make `g_rel_ok` and `g_busy` `std::atomic<bool>`.
* Alternatively disable the Install button until the check has finished or failed (it already shows "checking GitHub...").

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

The race itself is hard to hit in a test. Make it testable: move the release state into a small portable struct (`ReleaseState` with `get()` / `set()` under a mutex) in updater/, and add a host test in tests/net_tests.cpp, built with `-fsanitize=thread` in a new `make test-tsan` target, where two threads call `set()` 10 000 times with different `Release` values and a third calls `get()`. With today's plain assignment of a global (the test variant without the mutex), ThreadSanitizer reports a data race (FAIL); with the mutex it is clean (PASS). Plus a smoke check in `tools/tests/update_check.py`: run the setup silently with a delayed local `/api:` server and assert exit code 0.

### Related
None known.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #55.
- 2026-10-27: ticket opened from the issue.
