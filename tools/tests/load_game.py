#!/usr/bin/env python3
"""Load the saved creature-stage game of the test profile (fixture 'creature').

Positions are for the windowed 1440x1080 test profile (fractions of the screen).
usage: load_game.py [-i instance]
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from sporectl import Console  # noqa: E402
import lane  # noqa: E402

SLOT = (0.896, 0.530)   # saved planet on the galaxy menu
PLAY = (0.643, 0.500)   # play button in the planet panel


def main():
    inst = int(sys.argv[sys.argv.index('-i') + 1]) if '-i' in sys.argv else lane.HOST
    c = Console(inst)
    for _ in range(180):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 200 60000')
    w, h = 1440, 1080

    def click(f, frames=150):
        c.call('click %d %d' % (int(f[0] * w), int(f[1] * h)))
        c.call('wait.frames %d 30000' % frames)

    # the first click may only select/zoom to the planet: retry until the game starts
    for attempt in range(5):
        click(SLOT)
        click(SLOT)
        click(PLAY)
        started = False
        for _ in range(20):
            if 'Creature' in c.call('game.mode')['out']:
                started = True
                break
            time.sleep(1)
        if started:
            break
        print('retry', attempt + 1)
    for _ in range(180):
        if 'Creature' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    for _ in range(180):
        if c.call('coop.avatar')['ok']:
            break
        time.sleep(1)
    c.call('wait.frames 300 60000')
    print(c.call('coop.avatar')['out'])


if __name__ == '__main__':
    main()
