#!/usr/bin/env python3
"""Diagnostics for bug reports (SporeKit 0.3.4, docs/02-modkit.md 2.7), on one game of the lane:

  1. the log of the previous run is kept (mods/logs/previous/sporekit.N-<time>.log)
  2. the log starts with the environment: Windows / Wine, memory and the large-address flag, the game
     build, SporeCoop + SporeKit versions; after a few frames the GPU and other hooks
  3. `env` and `report` work; a report has every section
  4. the main menu shows "SporeCoop <version>" under the Co-op item, the co-op window shows the
     version line, and its "Report a problem" button (a real click) writes a report
  5. --crash: a call to address 1 kills the game: crash-<time>.txt with the exception, the call
     chain and the log, and a minidump next to it

  SPORE_LANE=2 python3 tools/tests/diag_check.py [--crash]
"""
import glob
import os
import re
import sys
import time

import lane
from coop_ui import VK_F9, Ui
from sporectl import Console
from twoinst import Checks, wait_up

GAME = os.path.expanduser('~/Games/Modding/SPORE') if lane.LANE == 1 else \
    os.path.expanduser(f'~/Games/Modding/SPORE-lab/lane{lane.LANE}')
LOGDIR = os.path.join(GAME, 'SporebinEP1', 'mods', 'logs')


def start_host(ck):
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.run_host()
    ck(wait_up(lane.HOST), 'the game starts')
    c = Console(lane.HOST)
    for _ in range(120):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 120 60000')
    return c


def logname():
    return os.path.join(LOGDIR, f'sporekit.{lane.HOST}.log' if lane.HOST > 1 else 'sporekit.log')


def main():
    ck = Checks()
    crash = '--crash' in sys.argv
    c = start_host(ck)          # a first run: makes a log to keep
    before = set(glob.glob(os.path.join(LOGDIR, 'previous', '*')))
    c = start_host(ck)          # the second run keeps the first one's log
    after = set(glob.glob(os.path.join(LOGDIR, 'previous', '*')))
    stem = os.path.basename(logname())[:-4]
    new = [p for p in after - before if os.path.basename(p).startswith(stem + '-')]
    ck(new, f'the previous log is kept ({[os.path.basename(p) for p in new]})')

    log = open(logname(), errors='replace').read()
    for what, rx in (('Windows / Wine', r'(Wine \S+ on|Windows \d+\.\d+ build)'),
                     ('memory and the large-address flag', r'game address space \d+ of \d+ MB used, large-address flag'),
                     ('the game build', r'game build: (GOG|Steam|the GOG|unknown)'),
                     ('the versions', r'SporeCoop \d+\.\d+\.\d+ on SporeKit \d+\.\d+\.\d+, game '),
                     ('the GPU and other hooks', r'GPU .*other hooks: ')):
        ck(re.search(rx, log), f'the log has {what}')

    env = c.call('env')['out']
    print('  env:', env.strip().replace('\n', ' | ')[:400])
    ck('SporeKit' in env and 'mods: ' in env and 'coop' in env, 'env names SporeKit, the game build and the mods')
    rep = c.call('report test')['out'].strip()
    path = os.path.join(LOGDIR, os.path.basename(rep.replace('\\', '/')))
    ck(os.path.exists(path), f'report writes {os.path.basename(path)}')
    if os.path.exists(path):
        txt = open(path, errors='replace').read()
        for sec in ('== Environment', '== coop', '== Faults caught before', '== Modules', '== The last log lines'):
            ck(sec in txt, f'the report has "{sec}"')
        ck('SporeCoop ' in txt and 'SporeApp.exe' in txt, 'the report names SporeCoop and lists the modules')

    # the menu: the version under the Co-op item; the window: the version line and the report button
    found = c.call('coop.ui find "SporeCoop 0."')['out'] + c.call('coop.ui find SporeCoop')['out']
    ck(re.search(r'SporeCoop \d+\.\d+\.\d+', found), f'the main menu shows the version ({found.strip()[:80]})')
    ui = Ui(c, 'host')
    c.call(f'key {VK_F9}')
    st = ui.wait(lambda s: s['open'] == '1' and s['report']['vis'], 20, 'the co-op window')
    ck(st is not None, 'the co-op window has "Report a problem"')
    n0 = len(glob.glob(os.path.join(LOGDIR, 'report-*.txt')))
    time.sleep(1.1)  # a new file name (seconds in it)
    ui.click('report')
    time.sleep(2)
    n1 = len(glob.glob(os.path.join(LOGDIR, 'report-*.txt')))
    ck(n1 > n0, f'"Report a problem" writes a report ({n0} -> {n1})')
    st = ui.state()
    print('  status after the click:', st.get('status', ''))
    c.call(f'key {VK_F9}')

    if crash:
        # a fault in the frame: the game's own handler takes it and the game stops drawing: a hang
        # report after 30 s without frames
        n0 = set(glob.glob(os.path.join(LOGDIR, 'report-*.txt')))
        try:
            c.call('call 0x1 cdecl')
        except (OSError, ValueError):
            pass
        hang = []
        for _ in range(50):
            time.sleep(1)
            hang = [p for p in set(glob.glob(os.path.join(LOGDIR, 'report-*.txt'))) - n0
                    if 'hang' in open(p, errors='replace').readline()]
            if hang:
                break
        ck(hang, f'a fault the game swallows (it stops drawing): a hang report ({[os.path.basename(p) for p in hang]})')
        # a fault nobody handles (a thread of its own): the crash report and the minidump
        c = start_host(ck)
        n0 = set(glob.glob(os.path.join(LOGDIR, 'crash-*')))
        try:
            c.call('crash.test thread')
        except (OSError, ValueError):
            pass
        new = set()
        for _ in range(30):
            time.sleep(1)
            new = set(glob.glob(os.path.join(LOGDIR, 'crash-*'))) - n0
            if any(p.endswith('.dmp') for p in new):
                break
        txts = [p for p in new if p.endswith('.txt')]
        ck(txts, f'an unhandled fault: a crash report ({[os.path.basename(p) for p in new]})')
        if txts:
            t = open(txts[0], errors='replace').read()
            ck(re.search(r'== Exception\n0xC0000005 access violation at 0x[0-9A-F]{8} sporekit\.dll\+0x', t)
               and 'write of address 0x00000010' in t, 'it names the exception, the module and the address')
            ck('== Call chain' in t and '== The last log lines' in t and '== coop' in t,
               'it has the call chain, the co-op state and the log')
            print('  ' + '\n  '.join(t.split('== Exception')[1].splitlines()[:4]))
        ck(any(p.endswith('.dmp') and os.path.getsize(p) > 10000 for p in new), 'a minidump is written next to it')
        lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
