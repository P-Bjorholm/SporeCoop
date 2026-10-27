---
id: BUG-0048
title: lab.py --branch can test another branch's worktree (prefix match)
status: open
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#26
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: #26
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/26 (full text, discussion).

`lab.py run ... --branch fix/cell` runs the tests in the worktree of `fix/cell-meat` if that one exists. The run directory and summary are named after `fix/cell`, and that summary is the evidence for a merge (docs/10-dev-pipeline.md: "before a merge to main: `lab.py run smoke --branch <b>` passes"), so one branch can be merged on another branch's results.

### Where
https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/lab.py#L39-L48

### How to reproduce
In a scratch clone:

```sh
git branch fix/cell
git worktree add ../wt-cellmeat -b fix/cell-meat
python3 -c "
import sys; sys.path.insert(0, 'tools'); import lab
print('fix/cell ->', lab.worktree('fix/cell'))
print('fix/cell-meat ->', lab.worktree('fix/cell-meat'))"
```

Output:

```
fix/cell -> <...>/wt-cellmeat
fix/cell-meat -> <...>/wt-cellmeat
```

### Expected
`fix/cell` has no worktree yet, so `worktree('fix/cell')` creates `../SporeCoop-wt/fix-cell`; it never returns another branch's tree.

## Root cause
**What happens**

```python
for block in git('worktree', 'list', '--porcelain').split('\n\n'):
    if f'branch refs/heads/{branch}' in block:
        return block.split('\n')[0].split(' ', 1)[1]
```

The test is a substring test: `branch refs/heads/fix/cell` is contained in `branch refs/heads/fix/cell-meat`. The first matching block wins, so a branch whose name is a prefix of another checked-out branch gets that one's tree. `run_id` still uses `a.branch`, so nothing in the summary name shows the mix-up (only the `head` line, from the wrong tree).

### Confidence
CONFIRMED by running.

## Fix
Not fixed yet. Suggested in the review:

Compare the line exactly:

```python
for block in git('worktree', 'list', '--porcelain').split('\n\n'):
    lines = block.split('\n')
    if f'branch refs/heads/{branch}' in lines:
        return lines[0].split(' ', 1)[1]
```

Also worth printing the branch of the chosen tree in the run header and in `summary.md`.

## Verification
Not done yet. Planned test (must FAIL without the fix and PASS with it):

A small unit test (no game) that makes two worktrees `fix/a-b` and a branch `fix/a` in a temporary clone and asserts `lab.worktree('fix/a')` is not the `fix/a-b` tree. It fails today and passes with the fix. Could run in `make test`.

### Related
CLAUDE.md section 4 ("`main` always works: only merged branches that passed `lab.py run smoke`"); docs/10-dev-pipeline.md 10.3.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #26.
- 2026-10-27: ticket opened from the issue.
