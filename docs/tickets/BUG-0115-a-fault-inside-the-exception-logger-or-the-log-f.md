---
id: BUG-0115
title: a fault inside the exception logger or the log freezes the game for good
status: open
kind: freeze
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#94
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #94
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/94 (full text, discussion).

SporeKit's vectored exception handler (`veh`) and its crash filter are not re-entrant. `veh` holds the exceptions `std::mutex` while it allocates memory, takes the loader lock (`GetModuleHandleExW`) and logs. `logv` holds the log `std::mutex` while it writes the file and changes the in-memory ring buffer (heap allocation and free). If a fault happens on a thread while it holds either mutex, `veh` runs again on the same thread and tries to take a mutex the thread already holds. With libc++ on llvm-mingw, `std::mutex` is an SRWLOCK, which cannot be taken twice, so the thread blocks forever. The game freezes with no crash report: the crash filter would need the log mutex too.

### Where
- `veh` takes the exceptions mutex, then does the risky work under it: `symbols_reverse` (allocates), `GetModuleHandleExW` (loader lock), `g_recs.push_back` (allocates), `logf` (log mutex): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/exceptions.cpp#L79-L162 (lock at L93; allocations L110, L120, L138; loader lock L113; logging L145-L159)
- `logv` holds the log mutex across `fputs`/`fflush` and the deque push/pop: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/log.cpp#L64-L85 (lock at L76)
- The crash filter logs before anything else: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L622-L628

### How to reproduce
1. A test-only command that takes the log mutex and then faults, for example a `crash.test log` variant that calls `logf` with a callback which dereferences null under the lock. Or a debug hook that faults inside `veh` after L93.
2. Expected with the bug: the game freezes (watchdog "no frames" after 30 s) and no `crash-*.txt` is written.
3. In real play the window is "a fault while one of the two locks is held", most likely on a heap that is already corrupt. The known issue "Creature chaos: a guest crashed with a corrupted heap" ends in a freeze inside the allocator. It is not shown that SporeKit's locks were involved there.

### Expected
A fault inside SporeKit's own fault logging is skipped (the handler returns `EXCEPTION_CONTINUE_SEARCH` at once). The crash filter still writes its short report.

## Root cause
**What happens**

The two racing sides are the same thread, re-entered by a fault.

- **Log mutex:** a thread is inside `logv`'s locked part and faults. On a corrupted heap that can happen in `g_ring.emplace_back` / `pop_front`, or in the CRT's `FILE` code. The first-chance fault calls `veh`, then `logf` (L145), then `logv` locks the log mutex again: deadlock. Logging is very frequent, so a corrupted process heap is likely to show up there.
- **Exceptions mutex:** a fault inside `veh` itself, for example in the allocation of `r.where` or in `g_recs.push_back`, calls `veh` again. It locks the exceptions mutex again: deadlock.
- **Across threads:** thread A is in `veh` holding the exceptions mutex and waits for the loader lock in `GetModuleHandleExW`. Thread B holds the loader lock (inside `LoadLibrary`, or a DllMain during thread start) and takes a first-chance fault. B's `veh` waits for the exceptions mutex. Neither thread ever runs again.

In every case the thread hangs instead of reaching the crash filter, so there is no crash report. If it is the main thread, the game stops drawing for good. The watchdog may then write a "no frames" dump, but that dump can freeze as well (#28).

### Confidence
- The mechanism (non-recursive SRWLOCK-based `std::mutex` taken again by the same thread from `veh`): CONFIRMED by reading.
- That a fault happens inside those regions in practice: PLAUSIBLE.

A second reviewer verified the mechanism. Severity: low.

## Fix
Not fixed yet. Suggested in the review:

- A `thread_local` re-entry flag in `veh` and in `crash_filter`: when it is already set, return `EXCEPTION_CONTINUE_SEARCH` without locking or logging.
- In `veh`, copy the raw record under the lock only. Do the symbol lookup, the `GetModuleHandleExW` and the logging outside it, or later on the main thread (`exceptions_pump`).
- In the crash filter, write the short report without `logf` (no locks at all), or try the log lock with a timeout.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A diag test (next to `tools/tests/diag_check.py`) with a test-only command that faults while the log mutex is held. It asserts that the game is still drawing frames afterwards (or wrote a crash report) and is not frozen. On main it should freeze (FAIL); with the fix it should PASS.

### Related
#28 (stack dump can freeze the game for good), #34 (a stack overflow ends the game without a crash report).

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #94.
- 2026-10-27: ticket opened from the issue.
