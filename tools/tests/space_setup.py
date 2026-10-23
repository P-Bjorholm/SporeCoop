#!/usr/bin/env python3
"""From the galaxy menu: start a new Space stage game, skip the flight tutorial, leave the home
planet (the game only saves outside planets) and save. Afterwards: spore.sh fixture save space.

usage: space_setup.py [-i N]
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
from sporectl import Console  # noqa: E402
import lane  # noqa: E402


def main():
    inst = int(sys.argv[sys.argv.index('-i') + 1]) if '-i' in sys.argv else lane.HOST
    r = subprocess.run([sys.executable, os.path.join(HERE, 'new_stage_game.py'), 'space', '-i', str(inst)])
    if r.returncode:
        return 1
    c = Console(inst)
    run = lambda line: c.call(line).get('out', '')  # noqa: E731
    for _ in range(3):  # intro texts
        run('wait.frames 200 60000')
        run('click 1410 1046')
    run('wait.frames 200 60000')
    run('click 290 914')  # mission control
    run('wait.frames 100 60000')
    for y in (755, 755, 723, 723, 723):  # "been there, done that", "ready", "what's next", "I accept", "bye"
        run(f'click 720 {y}')
        run('wait.frames 150 60000')
    for attempt in range(4):
        for _ in range(4):
            run('wheel 720 540 -25')
            run('wait.frames 150 60000')
        run('wait.frames 300 60000')
        run('click 1410 1046')  # badge
        if 'eax=0x00000001' in run('call Simulator::GetCurrentContext cdecl'):
            break
    else:
        print('FAILED: still on the planet')
        return 1
    print(run('coop.save'))
    run('wait.frames 300 60000')
    run('click 980 606')  # "game saved" box
    print(run('coop.avatar').splitlines()[0])
    return 0


if __name__ == '__main__':
    sys.exit(main())
