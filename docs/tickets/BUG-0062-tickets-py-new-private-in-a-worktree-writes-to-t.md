---
id: BUG-0062
title: tickets.py new --private in a worktree writes to that worktree's out/ and the ticket is lost
status: open
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#40
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #40
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/40 (full text, discussion).

A private ticket (an open security problem, or anything with player data) opened by an agent in its worktree goes to `<worktree>/out/tickets/`. `find` and `list` from the main checkout do not see it, and a plain `git worktree remove` deletes it without a warning, because `out/` is ignored by git. After that its number is free again. So exactly the tickets that must not be lost (security) can disappear when an orchestrator cleans up worktrees.

### Where
- `PRIVATE` is relative to the worktree's own root: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tickets.py#L23-L25
- `cmd_new` writes there: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tickets.py#L155-L171
- The docstring that says otherwise ("the main checkout's out/tickets"): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tickets.py#L117-L120

### How to reproduce
In a scratch clone:

```sh
git worktree add ../wt-sec -b fix/sec
(cd ../wt-sec && python3 tools/tickets.py new "open security hole" --kind security --private)
python3 tools/tickets.py find "security hole"      # prints nothing
git worktree remove ../wt-sec                       # succeeds, no warning
python3 tools/tickets.py new "next" --private
```

Output:

```
.../wt-sec/out/tickets/BUG-0028-open-security-hole.md
.../out/tickets/BUG-0028-next.md
```

The first file is gone with the worktree, and its id was given out again.

### Expected
Private tickets always live in one place shared by all worktrees (the main checkout's `out/tickets/`), and `list`/`find` from any worktree see them.

## Root cause
**What happens**

```python
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRIVATE = os.path.join(ROOT, 'out', 'tickets')
...
d = PRIVATE if a.private else PUBLIC
```

`ROOT` is the worktree the script was run from. `all_tickets()` (used by `list`, `find`, `show`, `release`, `check`) reads only `ROOT/docs/tickets` and `ROOT/out/tickets`, so the main checkout never sees a worktree's private tickets. `git worktree remove` refuses only for modified or untracked files; ignored files such as `out/` are deleted.

### Confidence
CONFIRMED by running.

## Fix
Not fixed yet. Suggested in the review:

Derive the private folder from the common git dir, like the lock does:

```python
common = git('rev-parse', '--path-format=absolute', '--git-common-dir')
MAIN = os.path.dirname(common)
PRIVATE = os.path.join(MAIN, 'out', 'tickets')
```

and use that `PRIVATE` in `all_tickets()`, `cmd_new`, `cmd_release`. Optionally warn in the merge-batch skill to check `out/tickets` of a worktree before removing it, for tickets written by older versions.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

Temporary repo test: create a private ticket in a worktree, remove the worktree, run `tickets.py find` from the main checkout: the ticket must be found. Today it is not.

### Related
CLAUDE.md section 1 ("Open security problems: `--private` (out/tickets/)"; "Agents open tickets themselves ... in their own worktree"). The id reuse of public tickets is filed separately.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #40.
- 2026-10-27: ticket opened from the issue.
