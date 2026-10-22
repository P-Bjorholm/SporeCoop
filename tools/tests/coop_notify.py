#!/usr/bin/env python3
"""Co-op notification panels: for every stage fixture start one game, load the planet and show a
few notifications; screenshots in out/shots/notify_<stage>.png.

usage: coop_notify.py [stage ...]   (default: creature tribe civ space cell)
"""
import os
import re
import subprocess
import sys
import time

from twoinst import Checks, ROOT, shot, wait_up
from sporectl import Console

FIXTURES = {  # stage -> (fixture, planet)
    'cell': ('cell', 'Game0/Karke'),
    'creature': ('creature', None),
    'tribe': ('tribe', 'Game0/Cohrach'),
    'civ': ('civ', 'Game0/Agrauel'),
    'space': ('space', 'Game0/Maraya'),
}
SPORE = os.path.join(ROOT, 'tools', 'spore.sh')


def sh(*a):
    subprocess.run([SPORE, *a], check=True, stdout=subprocess.DEVNULL)


def start(fixture, planet):
    sh('kill')
    time.sleep(2)
    sh('deploy')
    sh('fixture', 'restore', fixture, 'test')
    sh('run', '-p', 'test')
    assert wait_up(1), 'the game did not start'
    c = Console(1)
    if planet:
        for _ in range(180):
            if 'GalaxyGameEntry' in c.call('game.mode')['out']:
                break
            time.sleep(1)
        c.call('wait.frames 240 90000')
        slot, name = planet.split('/')
        c.call(f'coop.load {slot} {name}')
    else:
        subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'load_game.py'), '-i', '1'],
                       capture_output=True, text=True, timeout=600)
    t0 = time.time()
    while time.time() - t0 < 240 and not c.call('coop.avatar')['ok'] and 'Galaxy' in c.call('game.mode')['out']:
        time.sleep(2)
    time.sleep(25)  # intro / hatching cinematics
    return c


def main():
    stages = sys.argv[1:] or ['creature', 'tribe', 'civ', 'space', 'cell']
    ck = Checks()
    for st in stages:
        fx, planet = FIXTURES[st]
        print(f'== {st}', flush=True)
        c = start(fx, planet)
        print('  mode:', c.call('game.mode')['out'].strip())
        for text in ('Bob joined the pack', 'Bob is knocked out: swim right next to them within 10 s',
                     'Alice revived Bob', 'Bob: hi from the chat'):
            c.call('coop.notify ' + text)
            time.sleep(0.6)
        time.sleep(1.5)
        out = c.call('coop.status')['out']
        m = re.search(r'notify: shown=(\d+) failed=(\d+) visible=(\d+)', out)
        print(' ', m.group(0) if m else out[-300:])
        rects = [tuple(int(float(v)) for v in r) for r in re.findall(r'toast \w+ \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', out)]
        print('  panels:', rects)
        ck(m and int(m.group(1)) >= 4 and m.group(2) == '0', f'{st}: notification panels built')
        ck(len(rects) >= 4 and all(0 < r[0] and r[2] <= 1440 and 0 < r[1] < 1080 for r in rects),
           f'{st}: panels on the screen')
        ck(len({r[1] for r in rects}) == len(rects), f'{st}: stacked, not on top of each other')
        shot(c, f'notify_{st}.png')
        ck.faults(game=c)
    sh('kill')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
