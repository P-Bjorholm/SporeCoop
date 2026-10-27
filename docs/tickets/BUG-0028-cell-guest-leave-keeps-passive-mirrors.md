---
id: BUG-0028
title: Cell guest leaving the session keeps passive mirrors
status: fixed
kind: bug
stage: Cell
reported: 2026-10-27
source: known issue and code review
role: guest
versions: 
fixed_in: unreleased
branch: fix/cell-guest-leave
commits: 
tests: cell_coop
signature: 
docs: docs/re/cell.md
related: BUG-0006
---

## Report
When the guest leaves in the Cell stage, the host's mirrored cells remain in its pool. They
disappear when the guest leaves the stage or rejoins.

## Root cause
The guest's `do_leave` does not call `cellsync::on_session_end`; the disconnection callback does.

## Fix
Run Cell cleanup when a player leaves voluntarily. The cleanup removes mirrors only while the
Cell pool is valid, then forgets them.

## Verification
`cell_coop` checks the local pool indices before and after the guest leaves.

## History
- 2026-10-27: recorded from the known issue.
- 2026-10-27: fixed on fix/cell-guest-leave, not released yet.
