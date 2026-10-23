#!/usr/bin/env python3
"""Test lab: runs the automated tests on several lanes at once (docs/10-dev-pipeline.md).

  lab.py lanes 3                       create lanes 2..3 (lane 1 = the real install)
  lab.py run smoke --lanes 3           build, then the smoke suite on lanes 1..3
  lab.py run full --lanes 3 --branch feat/x   the same for a branch (its own worktree + build)
  lab.py run tribe_play,select         chosen tests (names: tools/tests/suite.py)
  lab.py reload --lanes 2              deploy the build and hot-reload coop.dll in running games
  lab.py kill [--lanes 3]              stop the games of the lanes

A lane runs one test at a time. Warm tests reuse the lane's running games (SPORE_WARM=1,
twoinst.start: ~10 s instead of ~40 s); after a failure the lane restarts cold. A test that dies
without a RESULT line (harness trouble: start, timeout) is retried once cold and marked flaky
when it passes then. Exclusive tests run at the end, alone. Results: out/lab/<run>/ (one log per
test, results.json, summary.md).
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'tests'))
import suite  # noqa: E402

TIMEOUT = 1800


def git(*a, cwd=ROOT):
    return subprocess.run(['git', *a], cwd=cwd, capture_output=True, text=True, check=True).stdout.strip()


def worktree(branch):
    """the checkout of a branch: the main one if it is checked out there, else a worktree next to it"""
    if git('branch', '--show-current') == branch:
        return ROOT
    for block in git('worktree', 'list', '--porcelain').split('\n\n'):
        if f'branch refs/heads/{branch}' in block:
            return block.split('\n')[0].split(' ', 1)[1]
    path = os.path.join(os.path.dirname(ROOT), 'SporeCoop-wt', branch.replace('/', '-'))
    git('worktree', 'add', path, branch)
    return path


def spore(tree, lane, *a, check=False):
    env = dict(os.environ, SPORE_LANE=str(lane))
    return subprocess.run([os.path.join(tree, 'tools', 'spore.sh'), *a], env=env, capture_output=True, text=True,
                          check=check)


def build(tree):
    r = subprocess.run(['make', '-j16'], cwd=tree, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-3000:], r.stderr[-3000:])
        sys.exit('build failed')


def ensure_lanes(n):
    for lane in range(2, n + 1):
        print(spore(ROOT, 1, 'lane', 'create', str(lane), check=True).stdout.strip())


def run_one(t, lane, tree, logdir, warm, suffix=''):
    env = dict(os.environ, SPORE_LANE=str(lane), SPORE_WARM='1' if warm else '0', PYTHONUNBUFFERED='1')
    log = os.path.join(logdir, t['name'] + suffix + '.log')
    t0 = time.time()
    with open(log, 'w') as f:
        f.write(f'# {t["cmd"]}  (lane {lane}, {"warm" if warm else "cold"})\n')
        f.flush()
        try:
            rc = subprocess.run(t['cmd'], shell=True, cwd=tree, env=env, stdout=f, stderr=subprocess.STDOUT,
                                timeout=TIMEOUT).returncode
        except subprocess.TimeoutExpired:
            rc = -9
            f.write('\nLAB: TIMEOUT\n')
    text = open(log, errors='replace').read()
    has_result = 'RESULT:' in text or t['cmd'].startswith('make')
    ok = rc == 0 and 'RESULT: FAIL' not in text and 'Traceback' not in text and has_result
    m = re.search(r'\((both games ready|warm reset) in (\d+) s', text)
    return dict(name=t['name'], lane=lane, ok=ok, rc=rc, harness=not has_result or 'Traceback' in text,
                seconds=round(time.time() - t0), start=f'{m.group(1)} {m.group(2)} s' if m else '',
                fails=[l.strip() for l in text.splitlines() if l.strip().startswith('FAIL')][:8],
                faults=[l.strip() for l in text.splitlines() if ' faults: ' in l and 'total faults: 0' not in l],
                log=log)


def cmd_run(a):
    tree = worktree(a.branch) if a.branch else ROOT
    tests = suite.select(a.tests)
    run_id = datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + ('-' + a.branch.replace('/', '-') if a.branch else '')
    logdir = os.path.join(ROOT, 'out', 'lab', run_id)
    os.makedirs(logdir, exist_ok=True)
    head = git('log', '-1', '--format=%h %s', cwd=tree)
    print(f'lab run {run_id}: {len(tests)} tests on {a.lanes} lane(s), tree {tree} ({head})')
    if not a.no_build:
        build(tree)
    ensure_lanes(a.lanes)
    for lane in range(1, a.lanes + 1):  # a run starts from clean games
        spore(tree, lane, 'kill')
    parallel = sorted([t for t in tests if not t['exclusive']], key=lambda t: -t['minutes'])
    alone = [t for t in tests if t['exclusive']]
    pending = list(parallel)
    results, lock = [], threading.Lock()

    def take(last_fixture):
        """the next test for a lane: one on the save the lane has loaded (warm), else the longest"""
        with lock:
            if not pending:
                return None
            same = [t for t in pending if t['warm'] and last_fixture and t['fixture'] == last_fixture]
            t = same[0] if same else pending[0]
            pending.remove(t)
            return t

    def report(r):
        with lock:
            results.append(r)
            flag = 'OK  ' if r['ok'] else 'FAIL'
            extra = ' (flaky)' if r.get('flaky') else ''
            print(f'  {flag} {r["name"]:<15} lane {r["lane"]}  {r["seconds"]:>4} s  {r["start"]}{extra}'
                  + ''.join(f'\n         {x}' for x in r['fails'] + r['faults']), flush=True)

    def attempt(t, lane, warm_ok):
        r = run_one(t, lane, tree, logdir, t['warm'] and warm_ok and not a.cold)
        if not r['ok'] or not t['warm']:
            spore(tree, lane, 'kill')  # a clean slate after a failure or a test with its own setup
            warm_ok = False
        else:
            warm_ok = True
        if not r['ok'] and r['harness'] and a.retry:
            r2 = run_one(t, lane, tree, logdir, False, '.retry')
            r2['flaky'] = r2['ok']
            r2['first'] = r['fails'] or ['no RESULT (harness)']
            spore(tree, lane, 'kill')
            return r2, False
        return r, warm_ok

    def worker(lane):
        warm_ok, last = False, None  # every lane starts cold
        while True:
            t = take(last if warm_ok else None)
            if t is None:
                return
            r, warm_ok = attempt(t, lane, warm_ok)
            last = t['fixture']
            report(r)

    t0 = time.time()
    threads = [threading.Thread(target=worker, args=(lane,)) for lane in range(1, a.lanes + 1)]
    for th in threads:
        th.start()
        time.sleep(5)  # Bottles sandboxes starting at the same moment can race
    for th in threads:
        th.join()
    if alone:  # the machine alone: no game of another lane may be visible (LAN discovery)
        for lane in range(1, a.lanes + 1):
            spore(tree, lane, 'kill')
    for t in alone:
        report(attempt(t, 1, False)[0])
    for lane in range(1, a.lanes + 1):
        spore(tree, lane, 'kill')
    total = time.time() - t0
    ok = all(r['ok'] for r in results)
    json.dump(dict(run=run_id, tree=tree, head=head, lanes=a.lanes, seconds=round(total), ok=ok, results=results),
              open(os.path.join(logdir, 'results.json'), 'w'), indent=1)
    lines = [f'# lab run {run_id}', '', f'{head} — {len(results)} tests, {a.lanes} lanes, {total / 60:.1f} min, '
             + ('**all passed**' if ok else '**FAILED**'), '', '| test | result | lane | s | start |', '|---|---|---|---|---|']
    for r in sorted(results, key=lambda r: (r['ok'], r['name'])):
        lines.append(f'| {r["name"]} | {"ok" if r["ok"] else "FAIL"}{" (flaky)" if r.get("flaky") else ""} | {r["lane"]} '
                     f'| {r["seconds"]} | {r["start"]} |')
    open(os.path.join(logdir, 'summary.md'), 'w').write('\n'.join(lines) + '\n')
    print(f'{"PASSED" if ok else "FAILED"}: {sum(r["ok"] for r in results)}/{len(results)} in {total / 60:.1f} min '
          f'-> out/lab/{run_id}/summary.md')
    return 0 if ok else 1


def cmd_reload(a):
    """the dev loop: rebuild, copy the DLLs into each lane and hot-reload coop.dll in its games"""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    from sporectl import Console
    build(ROOT)
    for lane in range(1, a.lanes + 1):
        spore(ROOT, lane, 'deploy')
        for inst in (2 * lane - 1, 2 * lane):
            try:
                c = Console(inst, timeout=30)
                out = c.call('mod.reload coop')['out'].strip()
                c.call('sym.reload')
                print(f'  lane {lane} instance {inst}: {out[:80]}')
            except OSError:
                print(f'  lane {lane} instance {inst}: not running')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run')
    r.add_argument('tests', nargs='?', default='smoke', help='smoke | full | nightly | name,name')
    r.add_argument('--lanes', type=int, default=int(os.environ.get('SPORE_LANES', '3')))
    r.add_argument('--branch')
    r.add_argument('--cold', action='store_true', help='no warm reuse of running games')
    r.add_argument('--no-build', action='store_true')
    r.add_argument('--retry', type=int, default=1)
    ln = sub.add_parser('lanes')
    ln.add_argument('n', type=int)
    rl = sub.add_parser('reload')
    rl.add_argument('--lanes', type=int, default=1)
    k = sub.add_parser('kill')
    k.add_argument('--lanes', type=int, default=5)
    a = ap.parse_args()
    if a.cmd == 'run':
        return cmd_run(a)
    if a.cmd == 'lanes':
        ensure_lanes(a.n)
    if a.cmd == 'reload':
        cmd_reload(a)
    if a.cmd == 'kill':
        for lane in range(1, a.lanes + 1):
            spore(ROOT, lane, 'kill')
    return 0


if __name__ == '__main__':
    sys.exit(main())
