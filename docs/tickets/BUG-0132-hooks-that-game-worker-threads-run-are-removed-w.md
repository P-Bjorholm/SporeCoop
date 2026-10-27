---
id: BUG-0132
title: hooks that game worker threads run are removed without stopping those threads
status: open
kind: crash
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#111
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #111
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/111 (full text, discussion).

SporeKit's MinHook has thread freezing switched off by design: it deadlocked under Wine. The assumption is that hooks are installed and removed from the main thread while no other thread runs the hooked code. That does not hold for every hook. `coop.dll` hooks `Math::RandomNumberGenerator::RandomInt` and `RandomFloat`, and the game's worker threads draw from the global RNG too (docs/re/space.md 7.3). At quit (`App::cSporeApp::Shutdown` calls `mods_shutdown`) and on hot reload, `hooks_remove_all` restores the original bytes and frees the trampolines, and then `FreeLibrary(coop.dll)` runs. All of this happens while a worker thread may be:
- executing the 5 bytes being rewritten,
- running inside the trampoline being freed,
- inside `h_RandomInt` and about to return into the unloaded DLL.

### Where
- Removal of all hooks of a mod (no freezing, no wait): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/hooks.cpp#L78-L89
- MinHook patch: freezing off by default ("SporeKit installs hooks from the main thread between frames"): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/third_party/minhook/src/hook.c#L327-L343
- Unload order (`on_unload`, `hooks_remove_all`, then `FreeLibrary`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/mods.cpp#L70-L86
- Quit path: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/gamehooks.cpp#L59-L65
- The other side, hooks a worker thread can run (`h_RandomInt` / `h_RandomFloat`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L478-L486 and their installation https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L939-L946

### How to reproduce
- The window is a few nanoseconds per call and exists only at quit or reload, so it is rare in play.
- To widen it for a test: a test-only command that starts a thread calling `RandomInt` on the global RNG in a tight loop (the function at `Math::RandomNumberGenerator::RandomInt` with `Math::RandomNumberGenerator::global_ptr`). Then run `mod.reload coop` 50 times through the console and count crash reports or faults in `exc`.

### Expected
Unloading or quitting never faults on another thread, whatever that thread was calling.

## Root cause
**What happens**

The racing sides are the main thread in `hooks_remove_all` / `FreeLibrary`, and a game worker thread calling RandomInt or RandomFloat.

1. `MH_DisableHook` copies the saved prologue back over the `jmp`. A thread that is decoding those bytes at that moment can execute a mix of old and new bytes.
2. `MH_RemoveHook` frees the trampoline. A thread that is executing the copied prologue in the trampoline jumps into freed memory.
3. `FreeLibrary(coop.dll)`. A thread that is inside `h_RandomInt` (it called `o_RandomInt` and has not returned) returns into an unmapped address.

Each case ends in an access violation on that thread, written as a crash report at quit or after `mod.reload`.

### Confidence
PLAUSIBLE. Freezing is off (checked by reading). That worker threads call the global RNG comes from docs/re/space.md 7.3. The window per call is tiny. A second reviewer verified the reading and the line numbers. Severity: low.

## Fix
Not fixed yet. Suggested in the review:

For hooks on functions other threads can call:
- Either do not remove them at unload: point the detour at a small stub owned by `sporekit.dll` that forwards to the original when the mod is gone. This is how `uiproc.cpp` keeps UI handlers alive across reloads.
- Or freeze threads for just those hooks with a safe method, for example by waiting until no thread is inside the detour, counted with an atomic in-flight counter, before `FreeLibrary`.
- At minimum, do not free the trampoline memory of such hooks.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

The reload loop with an RNG thread described above, counting faults (`exc`) and crash reports. It should FAIL on main within the loop, if the window can be hit, and PASS with the fix.

### Related
#38 (hot reload of coop.dll can crash while a thread or a queued task still runs old code).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #111.
- 2026-10-27: ticket opened from the issue.
