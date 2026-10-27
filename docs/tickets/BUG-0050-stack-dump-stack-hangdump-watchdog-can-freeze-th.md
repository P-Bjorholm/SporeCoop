---
id: BUG-0050
title: stack dump (stack, hangdump, watchdog) can freeze the game for good
status: open
kind: freeze
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#28
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #28
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/28 (full text, discussion).

The console commands `stack` and `hangdump`, and the hang watchdog, suspend a game thread and then allocate memory before they resume it. If the suspended thread holds the C runtime heap lock at that moment (any `malloc` / `new` in progress), the dumping thread blocks forever and the suspended thread is never resumed. The game is frozen for good, and the tool meant to diagnose hangs creates one. Severity: hang.

### Where
* `c_stack` (console command `stack`, flagged any-thread): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/commands.cpp#L428-L453
* `dump_thread_stack` (watchdog, `hangdump` through `watchdog_dump_now`): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/watchdog.cpp#L51-L91
* its automatic callers in the watchdog thread: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/watchdog.cpp#L95-L133
* registration of `stack` / `hangdump`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/commands.cpp#L651-L654

### How to reproduce
Timing-dependent; not reproduced on a running game yet.

1. Start a game instance with the console (`tools/lab.py` lane or `tools/spore.sh`).
2. While the game renders (for example in the creature stage), send `stack` repeatedly through the console (a loop of a few hundred calls).
3. At some call the console stops answering and the game freezes; `spore.sh kill` is needed.

A deterministic version needs the main thread to hold the CRT heap lock while `stack` runs (see **Test**).

### Expected
`stack`, `hangdump` and the watchdog dump never block while the target thread is suspended; the thread is always resumed within milliseconds.

## Root cause
**What happens**

1. `c_stack` opens the main thread and calls `SuspendThread(t)`.
2. While it is suspended, it calls `outf(...)` (appends to a `std::string`), `symbols_reverse(...)` (returns a new `std::string`) for every frame found, all of which allocate:
   ```cpp
   SuspendThread(t);
   ...
   outf(o, "eip=0x%08lX %s\n...", c.Eip, symbols_reverse(c.Eip).c_str(), ...);
   ...
   std::string r = symbols_reverse(v);
   outf(o, "  [esp+0x%04X] 0x%08X %s\n", i * 4, v, r.c_str());
   ...
   ResumeThread(t);
   ```
3. `dump_thread_stack` does the same: `SuspendThread(t)`, then `std::string out`, `out += b`, `describe()` (which calls `module_of` and `symbols_reverse`, both building strings), and only then `ResumeThread(t)`.
4. Allocation takes the heap lock. If the suspended thread was inside `malloc` / `free` / `operator new` (the main thread of Spore allocates constantly), the lock is owned by a thread that cannot run. The dumping thread waits on it forever and never reaches `ResumeThread`.
5. Result: the main thread stays suspended (game frozen, no frames, the window stops responding), and the console thread or watchdog thread is stuck too, so no further diagnostics are possible. The comment at the top of watchdog.cpp says it avoids the loader lock, but the heap lock has the same problem.

The automatic path matters most: the watchdog calls `dump_thread_stack` on the main thread after 15 s / 30 s without progress, and on the loader-lock owner after 10 s. A thread stuck inside `DllMain` often holds the heap lock as well.

### Confidence
CONFIRMED by reading the full path (suspend, allocate, resume order in both functions). Not verified on a running game; how often the main thread is inside the heap at a random instant is not measured.

## Fix
Not fixed yet. Suggested in the review:

While the thread is suspended, only copy raw data into fixed-size buffers on the stack: the `CONTEXT`, and up to N dwords from `esp` (checked with `VirtualQuery`, no allocation). Call `ResumeThread` right after, then format, resolve symbols and log. The same for `c_stack` and `dump_thread_stack`. `VirtualQuery` and `GetThreadContext` are fine while suspended; nothing that allocates or takes a user-mode lock is.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/kit_stack_dump.py` on one instance: add a debug command (for example `kit.holdheap <ms>`) that on the main thread locks the CRT heap (`HeapLock` on the handle from `_get_heap_handle()`, or a tight `malloc`/`free` loop as a weaker variant) for 3 s; while it holds it, call `stack` and `hangdump` from the console. Assert that both answer within 5 s, that frames continue afterwards (`frame` advances) and that no crash or hang report was written. Without the fix the console times out and the game stays frozen; with it both pass.

### Related
`stack.scan` was added because suspending the main thread under Wine could hang the console (commands.cpp comment above `c_stack_scan`); this may be the same or a second cause.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #28.
- 2026-10-27: ticket opened from the issue.
