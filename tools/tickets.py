#!/usr/bin/env python3
"""Project tickets: one Markdown file per bug report, crash or task in docs/tickets/ (CLAUDE.md 1).

  tickets.py new "<title>" --kind crash --stage Creature [--source ...] [--role guest]
                                         create the next BUG-NNNN file and print its path
  tickets.py list [--status open] [--kind crash] [--stage Creature]
  tickets.py show BUG-0012               the ticket file
  tickets.py find <text>                 tickets whose fields or text match (an address, a symptom, a test)
  tickets.py log BUG-0012                git commits that name the ticket ("BUG-0012" in the message)
  tickets.py index                       rewrite docs/tickets/README.md (the table of all tickets)
  tickets.py release 0.7.3               fixed tickets with fixed_in "unreleased" get that version
  tickets.py check                       (in make test) fields valid, ids unique, index up to date

Private tickets (security problems not fixed yet, anything with player data) go to
out/tickets/ (not in git) with --private; `list`/`find` read both places."""
import argparse
import os
import re
import subprocess
import sys
from datetime import date

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PUBLIC = os.path.join(ROOT, 'docs', 'tickets')
PRIVATE = os.path.join(ROOT, 'out', 'tickets')
INDEX = os.path.join(PUBLIC, 'README.md')
FIELDS = ['id', 'title', 'status', 'kind', 'stage', 'reported', 'source', 'role', 'versions', 'fixed_in',
          'branch', 'commits', 'tests', 'signature', 'docs', 'related']
STATUS = ['open', 'fixed', 'not-reproduced', 'game-bug', 'wontfix', 'duplicate']
KIND = ['crash', 'freeze', 'desync', 'bug', 'feature', 'security', 'test', 'tooling']
ID_RE = re.compile(r'^BUG-(\d{4})$')

INDEX_HEAD = """# Tickets

Every player report, crash and bug found gets a ticket: one file `BUG-NNNN-<slug>.md` here, made with
`tools/tickets.py new`. The file says what was reported, the root cause, the fix (branch, commits,
version) and the test that catches it; its `## History` grows by dated lines. Commit messages of a
fix name the ticket (`BUG-0012`), so `tools/tickets.py log BUG-0012` finds them. Security problems
that are not fixed yet stay in `out/tickets/` (not in git) until the fix is released.

This table is written by `tools/tickets.py index` (checked by `make test`). Statuses: open, fixed,
not-reproduced, game-bug (Spore itself), wontfix, duplicate.

| Ticket | Status | Kind | Stage | Reported | Fixed in | Title |
|---|---|---|---|---|---|---|
"""

TEMPLATE = """---
id: {id}
title: {title}
status: open
kind: {kind}
stage: {stage}
reported: {today}
source: {source}
role: {role}
versions: {versions}
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- {today}: opened.
"""


def parse(path):
    with open(path, encoding='utf-8') as f:
        text = f.read()
    m = re.match(r'---\n(.*?)\n---\n', text, re.S)
    meta = {}
    if m:
        for line in m.group(1).splitlines():
            if ':' in line:
                k, v = line.split(':', 1)
                meta[k.strip()] = v.strip()
    meta['_path'] = path
    meta['_text'] = text
    return meta


def all_tickets(private=True):
    out = []
    for d in [PUBLIC] + ([PRIVATE] if private else []):
        if not os.path.isdir(d):
            continue
        for n in sorted(os.listdir(d)):
            if re.match(r'^BUG-\d{4}-.*\.md$', n):
                t = parse(os.path.join(d, n))
                t['_private'] = d == PRIVATE
                out.append(t)
    return out


def slug(s):
    s = re.sub(r'[^a-z0-9]+', '-', s.lower()).strip('-')
    return s[:48].rstrip('-') or 'ticket'


def next_id():
    nums = [int(ID_RE.match(t.get('id', '')).group(1)) for t in all_tickets() if ID_RE.match(t.get('id', ''))]
    return f'BUG-{(max(nums) + 1) if nums else 1:04d}'


def index_text():
    rows = []
    for t in all_tickets(private=False):
        name = os.path.basename(t['_path'])
        rows.append(f"| [{t.get('id', '?')}]({name}) | {t.get('status', '')} | {t.get('kind', '')} | "
                    f"{t.get('stage', '')} | {t.get('reported', '')} | {t.get('fixed_in', '')} | "
                    f"{t.get('title', '').replace('|', '/')} |")
    return INDEX_HEAD + '\n'.join(rows) + '\n'


def cmd_new(a):
    d = PRIVATE if a.private else PUBLIC
    os.makedirs(d, exist_ok=True)
    tid = next_id()
    path = os.path.join(d, f'{tid}-{slug(a.title)}.md')
    with open(path, 'w', encoding='utf-8') as f:
        f.write(TEMPLATE.format(id=tid, title=a.title, kind=a.kind, stage=a.stage, today=date.today().isoformat(),
                                source=a.source, role=a.role, versions=a.versions))
    if not a.private:
        with open(INDEX, 'w', encoding='utf-8') as f:
            f.write(index_text())
    print(path)


def cmd_list(a):
    for t in all_tickets():
        if a.status and t.get('status') != a.status:
            continue
        if a.kind and t.get('kind') != a.kind:
            continue
        if a.stage and t.get('stage', '').lower() != a.stage.lower():
            continue
        p = ' (private)' if t['_private'] else ''
        print(f"{t.get('id')}  {t.get('status', ''):<14} {t.get('kind', ''):<8} {t.get('stage', ''):<12} "
              f"{t.get('fixed_in', '') or '-':<10} {t.get('title', '')}{p}")


def find_ticket(tid):
    for t in all_tickets():
        if t.get('id') == tid:
            return t
    sys.exit(f'no ticket {tid}')


def cmd_show(a):
    print(find_ticket(a.id)['_text'])


def cmd_find(a):
    rx = re.compile(re.escape(a.text), re.I)
    for t in all_tickets():
        if rx.search(t['_text']):
            hits = [l.strip() for l in t['_text'].splitlines() if rx.search(l)][:3]
            print(f"{t.get('id')}  {t.get('status')}  {t.get('title')}")
            for h in hits:
                print(f'    {h[:140]}')


def cmd_log(a):
    t = find_ticket(a.id)
    subprocess.run(['git', '-C', ROOT, 'log', '--all', '--format=%h %ad %s', '--date=short', f'--grep={a.id}'])
    for c in re.split(r'[,\s]+', t.get('commits', '')):
        if c:
            subprocess.run(['git', '-C', ROOT, 'log', '-1', '--format=%h %ad %s', '--date=short', c],
                           stderr=subprocess.DEVNULL)


def cmd_index(a):
    os.makedirs(PUBLIC, exist_ok=True)
    with open(INDEX, 'w', encoding='utf-8') as f:
        f.write(index_text())
    print(INDEX)


def cmd_release(a):
    n = 0
    for t in all_tickets():
        if t.get('status') == 'fixed' and t.get('fixed_in', '') in ('', 'unreleased'):
            text = re.sub(r'^fixed_in:.*$', f'fixed_in: {a.version}', t['_text'], count=1, flags=re.M)
            text = text.rstrip('\n') + f'\n- {date.today().isoformat()}: released in {a.version}.\n'
            with open(t['_path'], 'w', encoding='utf-8') as f:
                f.write(text)
            n += 1
            print(t.get('id'), t.get('title'))
    cmd_index(a)
    print(f'{n} ticket(s) marked fixed in {a.version}')


def cmd_check(a):
    problems, seen = [], {}
    for t in all_tickets():
        name = os.path.basename(t['_path'])
        tid = t.get('id', '')
        if not ID_RE.match(tid) or not name.startswith(tid + '-'):
            problems.append(f'{name}: id "{tid}" does not match the file name')
        if tid in seen:
            problems.append(f'{name}: id {tid} also in {seen[tid]}')
        seen[tid] = name
        for k in FIELDS:
            if k not in t:
                problems.append(f'{name}: field "{k}" missing')
        if t.get('status') not in STATUS:
            problems.append(f'{name}: status "{t.get("status")}" not one of {STATUS}')
        if t.get('kind') not in KIND:
            problems.append(f'{name}: kind "{t.get("kind")}" not one of {KIND}')
        if not re.match(r'^\d{4}-\d{2}-\d{2}$', t.get('reported', '')):
            problems.append(f'{name}: reported is not a date')
        if t.get('status') == 'fixed' and not (t.get('fixed_in') and t.get('tests')):
            problems.append(f'{name}: a fixed ticket needs fixed_in (a version or "unreleased") and tests')
        if t.get('kind') == 'security' and t.get('status') == 'open' and not t['_private']:
            problems.append(f'{name}: an open security ticket belongs in out/tickets/ (--private)')
    if os.path.isdir(PUBLIC):
        cur = open(INDEX, encoding='utf-8').read() if os.path.exists(INDEX) else ''
        if cur != index_text():
            problems.append('docs/tickets/README.md is not up to date: run tools/tickets.py index')
    for p in problems:
        print(p)
    print(f'tickets: {len(seen)} ticket(s), {len(problems)} problem(s)')
    return 1 if problems else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest='cmd', required=True)
    n = sp.add_parser('new')
    n.add_argument('title')
    n.add_argument('--kind', default='bug', choices=KIND)
    n.add_argument('--stage', default='')
    n.add_argument('--source', default='')
    n.add_argument('--role', default='')
    n.add_argument('--versions', default='')
    n.add_argument('--private', action='store_true')
    l = sp.add_parser('list')
    l.add_argument('--status')
    l.add_argument('--kind')
    l.add_argument('--stage')
    for c in ('show', 'log'):
        sp.add_parser(c).add_argument('id')
    sp.add_parser('find').add_argument('text')
    sp.add_parser('index')
    sp.add_parser('release').add_argument('version')
    sp.add_parser('check')
    a = ap.parse_args()
    r = globals()['cmd_' + a.cmd](a)
    sys.exit(r or 0)


if __name__ == '__main__':
    main()
