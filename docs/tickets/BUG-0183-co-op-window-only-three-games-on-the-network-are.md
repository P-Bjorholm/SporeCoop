---
id: BUG-0183
title: Co-op window: only three games on the network are listed
status: open
kind: bug
stage: Menu
reported: 2026-10-24
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
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

* The connect page shows the first three answers of the LAN discovery (`lan0`..`lan2`). On a
  machine running several lab lanes every host answers on 127.0.0.1 (and the LAN address), so the
  own lane's host can be the fourth and is not listed; a click on `lan0` then joined another lane's
  game ("rejected: different SporeApp.exe build", lane 9, 2026-10-24). `coop_ui.py` joins its own
  host by address when it is not listed and never clicks another lane's row. (The discovery also
  queried only ports 47810-47819, lanes 1-5: lane 9's host on 47826 was never found. It queries 20
  ports now, lanes 1-10.) Players with more than three co-op hosts on their
  network see only three: more rows or a filter (same build first) would help.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-24: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
