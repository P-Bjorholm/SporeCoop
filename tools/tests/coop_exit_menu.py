#!/usr/bin/env python3
"""The pause menu in co-op, through real input. 1) The host pauses for a while: the guest keeps
seeing the NPCs (until 2026-10-23 the host forgot every NPC 2 s into its pause, the death caption
or a cinematic, and the guest lost them all). 2) A guest leaves the host's world through the pause
menu ("Exit to Main Menu", "Don't Save" twice) and stays in the session: the host sends its world
again and the guest is back in the game (it used to wait in the galaxy menu forever). 3) The host
leaves to the main menu the same way: the guest is told, plays on alone (its own game runs its NPCs,
the host's frozen mirrors go, its DNA is kept) and gets the host's NPCs back when the host loads
the game again (until 2026-10-23 the guest stood on an empty planet without a word). 4) The guest
leaves the same way while the host's evolution vote runs, and the vote ends on the frame the host
starts to save its world for the guest again: the host's editor waits until the save is written
(until 2026-10-23 the editor opened while the game wrote that save, the save did not load and the
guest stayed in LoadGame forever).
Everything through real input: Esc, clicks on the menu buttons (found with coop.ui find)."""
import re
import time

from twoinst import Checks, lane_state, shot, start
from uifind import press, shown


def main():
    host, guest = start('creature')
    ck = Checks()

    def spawned():
        m = re.search(r'spawned=(\d+)', guest.call('coop.npc')['out'])
        return int(m.group(1)) if m else -1
    n0 = spawned()
    for _ in range(3):
        host.call('key esc')
        time.sleep(1.5)
        if shown(host, 'Resume Game'):
            break
    ck(shown(host, 'Resume Game') is not None, 'Esc opens the pause menu on the host')
    time.sleep(6)
    n1 = spawned()
    ck(n0 > 0 and n1 >= n0 - 1, f'the guest keeps the NPCs while the host is paused ({n0} -> {n1})')
    ck(press(host, 'Resume Game'), 'the host resumes')
    time.sleep(2)
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

    # 3) the host leaves to the main menu
    for _ in range(3):
        host.call('key esc')
        time.sleep(1.5)
        if shown(host, 'Resume Game'):
            break
    ok = press(host, 'Exit to Main Menu')
    for _ in range(2):  # "Do you want to save" / "Are you sure?" (none right after a save: the world
        time.sleep(1.5)  # was just saved for the guest)
        if shown(host, "Don't Save"):
            press(host, "Don't Save")
    ck(ok, 'the host leaves through the pause menu ("Exit to Main Menu")')
    away = None
    t0 = time.time()
    while time.time() - t0 < 60:
        st = guest.call('coop.status')['out']
        if 'host_away=1' in st:
            away = time.time() - t0
            break
        time.sleep(1)
    ck(away is not None, f'the guest knows the host is away ({away and round(away)} s)')
    ck('left for the main menu' in '\n'.join(guest.call('log 60')['out'].splitlines()),
       'the guest is told ("Alice (host) left for the main menu: you play on alone ...")')
    time.sleep(2)
    npc = guest.call('coop.npc')['out']
    print(' ', npc.strip().splitlines()[-1])
    ck('proxies=0' in npc and 'host_away=1' in npc, 'the host\'s frozen mirrors are gone, the guest\'s game runs its own')
    ck(guest.call('coop.avatar')['ok'], 'the guest plays on')
    guest.call('coop.dna add 5')  # DNA earned alone is kept ...
    time.sleep(1)
    solo = float(re.search(r'solo_dna=([\d.]+)', guest.call('coop.status')['out']).group(1))
    ck(solo >= 5, f'DNA earned alone is kept ({solo})')
    shot(guest, 'exit_menu_host_away.png')
    st = lane_state()
    slot, name = st.get('planet', 'Game0/Scalinus').split('/')
    if st.get('slot', 9) != 9:
        slot = f'Game{st["slot"]}'
    host.call(f'coop.load {slot} {name}')
    back = None
    t0 = time.time()
    while time.time() - t0 < 120:
        if host.call('coop.avatar')['ok'] and 'host_away=0' in guest.call('coop.status')['out'] and \
                'spawned=0' not in guest.call('coop.npc')['out']:
            back = time.time() - t0
            break
        time.sleep(1)
    ck(back is not None, f'the host is back: the guest mirrors its NPCs again ({back and round(back)} s)')
    time.sleep(3)
    ck('Bob earned 5 DNA' in host.call('log 80')['out'] and 'solo_dna=0.0' in guest.call('coop.status')['out'],
       '... and goes to the pack when the host is back')

    # 4) the guest leaves during the host's evolution vote
    print(' ', host.call('coop.vote editor')['out'].splitlines()[0])
    ck('vote active' in host.call('coop.vote')['out'], 'the host asks for its editor: the vote runs')
    time.sleep(2)
    for _ in range(3):
        guest.call('key esc')
        time.sleep(1.5)
        if shown(guest, 'Resume Game'):
            break
    ok = press(guest, 'Exit to Main Menu', 'Do you want to save your game?') and \
        press(guest, "Don't Save", 'Are you sure?') and press(guest, "Don't Save")
    ck(ok, 'the guest leaves to the main menu during the vote')
    n0 = host.call('log 400')['out'].count('back to the world')
    t0 = time.time()
    while time.time() - t0 < 40 and host.call('log 400')['out'].count('back to the world') == n0:
        time.sleep(0.05)
    # the vote ends now: the host's save for the guest is under way
    print(' ', host.call('coop.vote go')['out'].splitlines()[0], f'{time.time() - t0:.1f} s after the guest left')
    stuck = back = None
    t0 = time.time()
    while time.time() - t0 < 120:
        gm = guest.call('game.mode')['out']
        if 'LoadGame' in gm:
            stuck = stuck or time.time()
            if time.time() - stuck > 30:
                break
        else:
            stuck = None
        if 'Creature' in gm and guest.call('coop.avatar')['ok']:
            back = time.time() - t0
            break
        time.sleep(1)
    ck(stuck is None, 'the guest does not hang in LoadGame')
    ck(back is not None, f'the guest is back in the host world ({back and round(back)} s)')
    t0 = time.time()
    while time.time() - t0 < 30 and 'Editor' not in host.call('game.mode')['out']:
        time.sleep(1)
    ck('Editor' in host.call('game.mode')['out'], 'the host\'s editor opened after the vote')
    held = re.search(r'editor held for a save (\d+)', host.call('coop.vote')['out'])
    print(f'  editor held for the save: {held and held.group(1)}')
    shot(guest, 'exit_menu_vote_guest.png')
    host.call('click 1416 1062')  # leave the editor ("done"): the guest gets the evolved world
    t0 = time.time()
    together = False
    while time.time() - t0 < 120 and not together:
        together = 'Creature' in host.call('game.mode')['out'] and guest.call('coop.avatar')['ok'] \
            and 'together 1 1' in host.call('coop.status')['out'] and 'LoadGame' not in guest.call('game.mode')['out']
        time.sleep(2)
    ck(together, 'after the host\'s evolution both play together again')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
