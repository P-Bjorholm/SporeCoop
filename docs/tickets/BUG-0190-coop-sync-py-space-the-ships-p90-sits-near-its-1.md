---
id: BUG-0190
title: coop_sync.py space: the ships' p90 sits near its 10 m limit
status: open
kind: test
stage: Space
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: 
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* 2026-10-23, 4 runs: ships p90 7.4-10.6 m (one run over the limit), single samples up to 690 m.
  Ships fly fast and the test reads host and guest one after the other, so most of it is the time
  between the two reads plus the interpolation delay; the 690 m sample is the first sample after the start:
  the host's new ship still stands at its default spot (0, 0, 699) for that moment (play_space.py
  saw one 412 m sample in 2 h). The p90 of real play is
  1.2-8.3 m (docs/stability/space.md). Since then the test compares the guest with the host's path between
  its two reads (the guest is read in between); if it still fails, look at the samples, not at the limit.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests (wave 4): not reached: not measured with --repeat. Left open.
