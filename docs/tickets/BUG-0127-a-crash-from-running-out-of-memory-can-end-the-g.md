---
id: BUG-0127
title: a crash from running out of memory can end the game before the minidump and the "last game crashed" notice are written
status: open
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#106
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #106
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/106 (full text, discussion).

Spore is a 32-bit game and many of its crashes come from a full address space. The crash report thread builds the report archive (up to 24 MB of buffers, log tails read in 8 MB blocks) before it writes the minidump and before anything can catch an allocation failure. In a process that crashed because memory ran out, a `std::bad_alloc` in that thread is not caught. It ends the whole game through `std::terminate` / `abort`. The minidump is never written, and depending on where it happens, `logs\last-problem.txt` is not written either, so the next start does not tell the player that a report is ready.

### Where
- The crash report thread: report text, then archive, then mark, then minidump, then archive again: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L602-L620
- The archive builder: whole files read into vectors, then copied into the entry list: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L476-L560
- `read_file_tail` resizes a vector to the file size (up to the 8 MB / 24 MB caps): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/zip.cpp#L108-L127
- The filter waits up to 15 s for that thread: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/sporekit/diag.cpp#L622-L650

### How to reproduce
1. In a test build, make the archive allocation fail: e.g. reserve most of the free address space with `VirtualAlloc` in a debug command (or `crash.test` after allocating until `new` throws), then trigger `crash.test thread`.
2. The game ends at once. `mods\logs` holds the short or full `crash-*.txt` but no `.dmp`, no zip, and `last-problem.txt` is missing. The next start shows no crash notice.

### Expected
Even when memory is exhausted, the minidump (or at least `last-problem.txt` pointing at the crash report) is written, and an allocation failure in the report code never ends the game by itself.

## Root cause
**What happens**

- The order (report, archive, mark first; minidump last) is intentional: the comment at diag.cpp L602-L603 calls the minidump the riskiest step. That is reasonable for lock trouble, but it puts the step with the largest allocations first.
- The build uses C++ exceptions (Makefile, no `-fno-exceptions`), and nothing in `crash_report_thread` or `diag_write_bundle_with` catches them. When an allocation of several MB fails in a fragmented or full 32-bit address space, `bad_alloc` leaves the thread function: `std::terminate`, `abort`, and the process ends right away.
- Result for the player: `crash-<time>.txt` (written first, small) may exist, but there is no `.dmp`, no `SporeCoop-report-*.zip`, and no `last-problem.txt`, so no "the last game crashed, a report to send is ready" message at the next start. Out-of-memory crashes are exactly the ones whose minidump matters most.

### Confidence
PLAUSIBLE, severity low. CONFIRMED by reading that no handler exists and that the large allocations come before the minidump; that they fail in a real out-of-memory crash is inferred. A second reviewer verified the reading.

## Fix
Not fixed yet. Suggested in the review:

- Wrap the body of `crash_report_thread` (and `exit_report`, `diag_hang_report`) in `try { ... } catch (...) {}` per step, so a failed archive still lets the next step run.
- Call `mark_problem` with the crash `.txt` path before building the archive, then update it to the zip once that exists.
- Build the archive in a streaming way (write each entry to the file as it is read, in small blocks) instead of holding all entries in memory.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Extend `tools/tests/diag_check.py --crash` with an out-of-memory variant (a debug command that fills the address space before `crash.test thread`):
- Assert after the crash: a `.dmp` exists (or at least `last-problem.txt` names the crash report), and the next start shows the "last game crashed" notice.
- FAILS on main (no dump, no mark). PASSES with the fix.

### Related
- #34 (a stack overflow ends the game without a crash report)
- #28 (stack dump can freeze the game for good)

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #106.
- 2026-10-27: ticket opened from the issue.
