---
id: BUG-0060
title: hot reload of coop.dll can crash while the update check thread or a queued task still runs old code
status: open
kind: crash
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#38
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #38
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/38 (full text, discussion).

Every load of coop.dll starts a background update-check thread (WinHTTP to the GitHub API, timeouts up to 30 s). `on_unload` neither waits for that thread nor for a running update download. A `mod.reload coop` (the dev loop `tools/lab.py reload`) shortly after a load frees the DLL while the thread is still blocked in WinHTTP. When the thread returns, it jumps into unmapped memory or into the new copy at the same address. Outside-frame tasks queued by the old DLL have the same exposure. Severity: crash (developers and the lab; players do not hot reload).

### Where
- Threads started with no handle kept: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/autoupdate.cpp#L92-L104 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/autoupdate.cpp#L158-L175
- `on_unload` (no autoupdate shutdown): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2603-L2616
- SporeKit unload: `on_unload`, then hooks, events and commands removed, then `FreeLibrary`; the outside-frame queue is not touched: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/mods.cpp#L70-L86
- The queue holds plain function pointers into the mod: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/console.cpp#L38-L68
- Tasks the core queues every few frames: `menu_tick` (every 0.2 s), `coop_ui::tick` (refresh), `notify::tick`, and `submit_tick` / `own_editor_tick` / `finish_vote`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1360-L1367

### How to reproduce
1. One instance with network access (or offline with a slow DNS), `[update] check=1`.
2. `mod.reload coop` twice within 1-2 s (or run `tools/lab.py reload` twice quickly).
3. Expect, intermittently, a fault with an address inside the old coop.dll range in SporeKit's fault log, or a crash report.
4. Deterministic variant: point `[update] url=` at a local server that accepts the connection and never answers (`nc -l`), start the game, `mod.reload coop`, then stop the server so WinHTTP returns.

### Expected
`on_unload` returns only when no mod thread can run mod code any more, and queued tasks of the unloaded mod are dropped or never point into it.

## Root cause
**What happens**

1. `autoupdate::init` runs on every `sporekit_mod_main` and, with `[update] check=1` (the default; the lanes do not turn it off), calls `CreateThread(check_thread)` and closes the handle.
2. `check_thread` calls `upd::latest_release` and blocks in `WinHttpSendRequest` / `ReceiveResponse` (resolve and connect timeouts 10 s, receive 30 s; offline, DNS can take seconds).
3. `mod.reload coop` within that time: `on_unload` stops the keepalive thread but not this one. Then `FreeLibrary` runs, and SporeKit loads the new shadow copy, often at the same base.
4. WinHTTP returns into `check_thread`'s code, which belongs to the old image, and the thread then writes `g_state` / `g_rel` of the old image. Result: an access violation, or execution of unrelated code of the new image.
5. The same applies to `apply_thread` during "Update" (a download of several MB). It also applies to an outside-frame task queued by the old DLL that has not been pumped when the reload runs: `pump_outside_tasks` postpones the queue while a message is dispatched inside Present, and runs it later.

### Confidence
CONFIRMED by reading the full path (no join, no cancel, no state check at the thread's end). The crash itself is not reproduced here; it needs the timing above.

## Fix
Not fixed yet. Suggested in the review:

- autoupdate: keep the thread handles. Add `autoupdate::shutdown()` called from `on_unload`: set a cancel flag, close the WinHTTP handles (`WinHttpCloseHandle` on the request aborts a blocked call), then `WaitForSingleObject` the threads (bounded, e.g. 35 s, logging if it times out). Alternatively hold a module reference while the thread runs (`GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)` at the start and `FreeLibraryAndExitThread` at the end), so the image cannot be freed under it.
- Outside-frame tasks: SporeKit could tag tasks with the queuing mod (`run_outside_frame` has no `SkMod*`, so this needs an ABI addition) and drop them at unload. Or coop could gate every queued task on a generation counter that `on_unload` bumps. That only helps while the code is still mapped, so the SporeKit-side fix is the real one.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_reload_update.py` (one instance): a local HTTP server that holds the update-check request for 5 s, `mod.reload coop` during that time, release the request, then 10 s of frames. Assert the game is alive (frames advance, no crash report: the lab's crash check) and `coop.update` answers from the new instance. It must fail on main.

### Related
- The umbrella issue #11 (what `on_unload` leaves behind).
- `tools/tests/update_check.py`.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #38.
- 2026-10-27: ticket opened from the issue.
