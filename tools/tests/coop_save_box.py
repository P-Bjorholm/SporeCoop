#!/usr/bin/env python3
"""The host's "game saved" box is confirmed by the mod (world.cpp box_task) without touching other
menus. A save while the pause menu is open (a guest joins while the host pauses) must close the box
and leave the pause menu as it is; a player's own "Exit to Main Menu" right after a save must ask
"Do you want to save your game?" and wait for the player. Before 2026-10-23 the mod kept pressing
Enter for 30 s after each save: it answered that question itself and the host left the game
(and quit Spore after "Quit"), seen in chaos tests 3-4 s after such an Enter.

  SPORE_LANE=N python3 tools/tests/coop_save_box.py
"""
import re
import sys
import time

import uifind
from twoinst import Checks, start


def box_up(con):
    return any(re.search(r'id=0x06318E78 \S+ fl=0x([0-9A-Fa-f]+)', l) and
               int(re.search(r'fl=0x([0-9A-Fa-f]+)', l).group(1), 16) & 1
               for l in con.call('coop.ui tree 30 all')['out'].splitlines() if '06318E78' in l.upper())


def mode(con):
    return con.call('game.mode')['out'].split()[1]


def save_and_wait(ck, h, label):
    t0 = time.time()
    ck('saved' in h.call('coop.save')['out'], f'{label}: the host saves')
    seen = closed = False
    while time.time() - t0 < 20:
        up = box_up(h)
        seen |= up
        if seen and not up:
            closed = True
            break
        time.sleep(0.3)
    ck(seen and closed, f'{label}: the saved box came up and was confirmed ({time.time() - t0:.1f} s)')
    time.sleep(4)
    ck(mode(h) == 'Creature', f'{label}: the host is still in its game ({mode(h)})')


def main():
    h, g = start('creature')
    ck = Checks()
    save_and_wait(ck, h, 'plain save')
    h.call('key esc')
    for _ in range(10):
        if uifind.shown(h, 'Resume Game'):
            break
        time.sleep(0.5)
    ck(uifind.shown(h, 'Resume Game'), 'the pause menu is open')
    save_and_wait(ck, h, 'save with the pause menu open')
    ck(uifind.shown(h, 'Resume Game'), 'the pause menu is still open (nothing in it was pressed)')
    # the player leaves right after a save: the game asks, the mod must not answer
    asked = uifind.press(h, 'Exit to Main Menu', 'Do you want to save your game?', timeout=8)
    time.sleep(3)
    still = uifind.shown(h, 'Do you want to save your game?')
    ck(asked and still and mode(h) == 'Creature',
       f'"Exit to Main Menu" right after a save waits for the player (asked {asked}, still {bool(still)}, {mode(h)})')
    for _ in range(3):  # cancel the question and the pause menu
        if mode(h) != 'Creature' or not (uifind.shown(h, 'Do you want to save your game?') or uifind.shown(h, 'Resume Game')):
            break
        h.call('key esc')
        time.sleep(1)
    ck(mode(h) == 'Creature', 'back in the game')
    ck.faults(host=h, guest=g)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
