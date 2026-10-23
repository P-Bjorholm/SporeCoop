#!/usr/bin/env python3
"""Drive the main menu to start a fresh Creature-stage game (Maxis creature, easy),
skip the intro cinematic and the first tutorial popups.

Uses screen-relative click positions measured on the GOG build's galaxy menu.
usage: new_creature_game.py [-i instance] [--slot N]
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from sporectl import Console  # noqa: E402
import lane  # noqa: E402

# empty save-slot planets on the galaxy menu (fractions of the screen)
SLOTS = [(0.737, 0.403), (0.289, 0.750), (0.508, 0.665)]


def main():
    inst = lane.HOST
    slot = 0
    a = sys.argv[1:]
    if '-i' in a:
        inst = int(a[a.index('-i') + 1])
    if '--slot' in a:
        slot = int(a[a.index('--slot') + 1])
    c = Console(inst)
    for _ in range(180):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    if '--panel-open' not in a:
        c.call('wait.frames 700 60000')  # galaxy intro animation must finish
    tmp = os.path.join(lane.out_dir('shots'), 'menu.png')
    shot = c.call('shot ' + os.path.abspath(tmp))
    w, h = map(int, shot['out'].split()[-1].split('x'))

    def click(fx, fy, frames=90):
        c.call('click %d %d' % (int(fx * w), int(fy * h)))
        c.call('wait.frames %d 30000' % frames)

    print('screen %dx%d' % (w, h))
    c.call('cheat levels -unlock')
    sx, sy = SLOTS[slot]
    if '--panel-open' not in a:
        click(sx, sy, 120)      # open "Choose a stage" on an empty slot
    click(0.531, 0.4215)        # creature stage icon
    click(0.551, 0.500)         # herbivore
    click(0.215, 0.156)         # first Maxis creature in Sporepedia
    click(0.983, 0.983, 150)    # confirm
    click(0.643, 0.553)         # theme -> next
    click(0.643, 0.553)         # difficulty -> next
    click(0.645, 0.547, 60)     # name planet -> play
    for _ in range(180):
        if 'Creature' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    print(c.call('game.mode')['out'])
    for _ in range(120):
        if c.call('coop.avatar')['ok']:
            break
        time.sleep(1)
    c.call('wait.frames 600 60000')  # intro cinematic
    click(0.988, 0.977, 240)    # skip cinematic (green check)
    for fy in (0.581, 0.342, 0.354, 0.338):  # tutorial popups
        click(0.566, fy, 60)
    print(c.call('coop.avatar')['out'])


if __name__ == '__main__':
    main()
