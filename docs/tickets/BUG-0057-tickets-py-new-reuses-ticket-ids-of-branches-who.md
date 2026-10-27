---
id: BUG-0057
title: tickets.py new reuses ticket ids of branches whose worktree is gone
status: open
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#35
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #35
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/35 (full text, discussion).

`tools/tickets.py new` picks the next id by looking at `docs/tickets` in the worktrees that exist right now. A ticket committed on a branch whose worktree was removed (or that was never checked out here, or that main's checkout does not have yet) is not seen, so its number is handed out again. After the merge, `tickets.py check` (in `make test`) fails on the duplicate id, and commit messages naming `BUG-NNNN` point at two different bugs.

### Where
https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/tickets.py#L117-L142

### How to reproduce
In a scratch clone:

```sh
git worktree add ../wt-a -b fix/a
(cd ../wt-a && python3 tools/tickets.py new "found on the way" && git add -A && git commit -qm ticket)
git worktree remove ../wt-a
python3 tools/tickets.py new "another one"
```

Output:

```
.../wt-a/docs/tickets/BUG-0028-found-on-the-way.md
.../docs/tickets/BUG-0028-another-one.md
```

`git show --stat fix/a` still lists `docs/tickets/BUG-0028-found-on-the-way.md`.

### Expected
The second ticket is `BUG-0029`: an id is free only if no local branch (and no worktree, and no private ticket) uses it.

## Root cause
**What happens**

```python
def worktree_ticket_dirs():
    dirs = {PUBLIC, PRIVATE}
    out = subprocess.run(['git', '-C', ROOT, 'worktree', 'list', '--porcelain'], ...).stdout
    for line in out.splitlines():
        if line.startswith('worktree '):
            ...
```

Only files on disk in current worktrees count. Typical ways to lose a branch's tickets:
- an agent's worktree is removed after its commits and before the merge (the branch still exists);
- a branch was pushed from another machine or session and never checked out here;
- the main checkout is on another branch (as it is now), so tickets already merged into `main` are only in the `main` ref.

The lock (`tickets.lock` in the common git dir) is fine; the gap is in what is scanned.

### Confidence
CONFIRMED by running.

## Fix
Not fixed yet. Suggested in the review:

In `next_id`, also collect the names from every local branch (and the remote-tracking `main`):

```python
refs = git('for-each-ref', '--format=%(refname)', 'refs/heads', 'refs/remotes/origin/main').split()
for ref in refs:
    names = git('ls-tree', '--name-only', ref, 'docs/tickets/').split()
    nums += [int(m.group(1)) for n in names if (m := re.match(r'^docs/tickets/BUG-(\d{4})-', n))]
```

(`ls-tree` on all branches is fast; it reads trees, not files.)

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A unit test in a temporary repo doing the steps above: the second id must be `BUG-0029`. It fails today. It could run in `make test` next to `tickets.py check`.

### Related
CLAUDE.md section 1 ("the number is free across all worktrees"; "Agents open tickets themselves ... in their own worktree"). Commit 1694eb9 (lock in the common git dir).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #35.
- 2026-10-27: ticket opened from the issue.
