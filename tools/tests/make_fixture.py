#!/usr/bin/env python3
"""Make a new test save from an existing one: load the base fixture in the lane's host game, run
console commands (cheats, coop.* debug commands, waits), save, pack it as out/fixtures/<name>.tgz.
Then describe it in tools/tests/fixtures.py (planet, stage, facts).

  make_fixture.py <base> <name> [--cmd "line" ...] [--wait-frames N]
  e.g. make_fixture.py tribe tribe_rich --cmd "coop.res add food 500"
A stage-new save: tools/tests/new_stage_game.py <stage> then make_fixture.py from that.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import fixtures  # noqa: E402
import lane  # noqa: E402
from lane import sh  # noqa: E402
from sporectl import Console  # noqa: E402
from twoinst import wait_up  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('base')
    ap.add_argument('name')
    ap.add_argument('--cmd', action='append', default=[])
    ap.add_argument('--wait-frames', type=int, default=150)
    a = ap.parse_args()
    planet = fixtures.planet_of(a.base)
    assert planet, f'{a.base}: not in tools/tests/fixtures.py'
    sh('kill')
    sh('deploy')
    sh('fixture', 'restore', a.base, lane.HOST_PROFILE)
    lane.run_host()
    assert wait_up(lane.HOST), 'the game did not start'
    c = Console(lane.HOST)
    for _ in range(180):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 200 60000')
    slot, name = planet.split('/')
    print(c.call(f'coop.load {slot} {name}')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 240 and not c.call('coop.avatar')['ok']:
        time.sleep(2)
    c.call(f'wait.frames {a.wait_frames} 60000')
    for line in a.cmd:
        print('>', line, '\n ', c.call(line)['out'].strip()[:300])
        c.call('wait.frames 30 20000')
    print(c.call('coop.save')['out'].strip())
    c.call('wait.frames 120 60000')
    c.call('key enter')  # the "game saved" box
    c.call('wait.frames 30 20000')
    sh('fixture', 'save', a.name, lane.HOST_PROFILE)
    sh('kill')
    print(f'fixture {a.name}: out/fixtures/{a.name}.tgz - now describe it in tools/tests/fixtures.py')


if __name__ == '__main__':
    main()
