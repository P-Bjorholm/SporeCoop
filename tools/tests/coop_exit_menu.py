#!/usr/bin/env python3
"""A guest leaves the host's world through the pause menu ("Exit to Main Menu", "Don't Save" twice)
and stays in the session: the host sends its world again and the guest is back in the game.
Until 2026-10-23 the guest waited in the galaxy menu forever ("Waiting for the host's world...").
Everything through real input: Esc, clicks on the menu buttons (found with coop.ui find)."""
import time

from twoinst import Checks, shot, start
from uifind import press, shown


def main():
    host, guest = start('creature')
    ck = Checks()
    for _ in range(3):  # Esc opens the pause menu (a first Esc may close a card or tooltip)
        guest.call('key esc')
        time.sleep(1.5)
        if shown(guest, 'Resume Game'):
            break
    ck(shown(guest, 'Resume Game') is not None, 'Esc opens the pause menu on the guest')
    ck(press(guest, 'Exit to Main Menu', 'Do you want to save your game?'),
       'the guest clicks "Exit to Main Menu": "Do you want to save your game?"')
    ck(press(guest, "Don't Save", 'Are you sure?'), 'Don\'t Save: "Are you sure? All progress will be lost!"')
    ck(press(guest, "Don't Save"), 'Don\'t Save')
    t0 = time.time()
    in_menu = False
    back = None
    while time.time() - t0 < 120:
        mode = guest.call('game.mode')['out']
        if 'GalaxyGameEntry' in mode:
            in_menu = True
        if in_menu and 'Creature' in mode and guest.call('coop.avatar')['ok'] \
                and 'together 1 1' in host.call('coop.status')['out']:
            back = time.time() - t0
            break
        time.sleep(1)
    ck(in_menu, 'the guest went to the main menu')
    ck(back is not None, f'the guest is back in the host world ({back and round(back)} s)')
    ck('connected' in guest.call('coop.status')['out'] or 'Alice' in guest.call('coop.status')['out'],
       'the guest stayed in the session')
    time.sleep(5)
    npc = guest.call('coop.npc')['out']
    print(' ', npc.strip().splitlines()[-1])
    ck('spawned=0' not in npc, 'the guest mirrors the host NPCs again')
    shot(host, 'exit_menu_host.png')
    shot(guest, 'exit_menu_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
