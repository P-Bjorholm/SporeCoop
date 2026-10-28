#!/usr/bin/env python3
"""BUG-0149: digits typed in the cheat console are no social ability keys.

The guest engages a mirrored Kook (social stance, a click on it, as in coop_creature_social.py),
opens the cheat console (Ctrl+Shift+C) and types 1 and 2 there: the host gets no social act from
the guest (`acts_in` of the host's coop.social) and the guest's avatar plays nothing. After the
console is closed the key 1 works again (the host counts the act).

  coop_social_console.py [--without-fix]   (--without-fix: coop.social textfix off, must FAIL)

Not in suite.py yet: the console does not open reliably by injected keys (BUG-0149).
"""
import re
import sys
import time

from coop_creature_social import KOOK, engage, hold, num, social
from twoinst import Checks, shot, start


def console(con):
    for k in ('key ctrl down', 'key shift down', 'key c', 'key shift up', 'key ctrl up'):
        con.call(k)
        time.sleep(0.2)
    time.sleep(1.0)


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('creature')
    ck = Checks()
    if without:
        print(' ', guest.call('coop.social textfix off')['out'].strip())
    for c in (host, guest):
        c.call('coop.team hp 1000')
    time.sleep(3)
    mirrored = set(re.findall(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=' + KOOK + r' spawned', guest.call('coop.npc list')['out']))
    kooks = [m.group(1) for m in re.finditer(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=' + KOOK + r' hp=([\d.]+) dist=([\d.]+)(?! DEAD)',
                                             host.call('coop.npc list')['out'])
             if m.group(1) in mirrored and float(m.group(2)) > 0]
    target = None
    for k in kooks[:3]:
        if engage(guest, 'n' + k):
            target = k
            break
    ck(target is not None, f'the guest engaged a mirrored Kook (n{target})')
    if not target:
        return ck.result()

    def acts():
        return num(r'acts_in=(\d+)', social(host), 0)

    console(guest)
    keys = guest.call('coop.social keys')['out'].strip()
    print('  guest:', keys)
    # (2026-10-28: Ctrl+Shift+C opened the console on a lane game once, never again in 6 tries, also
    # not with the cheat manager's vt 0x44(1): the test stops here until a reliable way is found)
    ck('014A4606 none' not in guest.call('coop.ui where 014A4606')['out'], 'the cheat console is open on the guest')
    a0 = acts()
    for vk in (0x31, 0x32, 0x31):  # "121" typed into the console
        hold(guest, vk)
        time.sleep(0.5)
    time.sleep(2.5)
    a1 = acts()
    print(f'  host acts_in while the console was open: {a0} -> {a1}')
    shot(guest, 'social_console_guest.png')
    ck(a1 == a0, 'digits typed in the cheat console play no social ability (the host got no act)')
    console(guest)  # closes it
    time.sleep(1)
    guest.call(f'coop.social goto {target}')
    for _ in range(3):  # (the engagement may have run out meanwhile: engage again)
        if re.search(r'engaged=' + target + r'\b', social(guest)) or engage(guest, 'n' + target, 20):
            break
    a2 = acts()
    hold(guest, 0x31)
    time.sleep(2.5)
    a3 = acts()
    print(f'  host acts_in after the console was closed: {a2} -> {a3}')
    ck(a3 > a2, 'the key 1 plays the ability again once the console is closed')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
