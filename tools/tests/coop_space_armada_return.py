#!/usr/bin/env python3
"""Space: an armada wave survives the host leaving the planet (BUG-0046).

The armada contract's waves are the game's own pirate raids. The host leaves the colony during a
wave and lands again: the old code started a second raid on the planet while the first still ran
(the end of either decided the wave, the ships of both got their HP raised). Now the wave keeps
its raid mission and resumes it. Checked in the host's game: the raid missions on the colony
(`coop.space contracts raids`, the game's mission list, state 2 or 3) after the return: one.

  SPORE_LANE=6 python3 tools/tests/coop_space_armada_return.py [--without-fix]
"""
import re
import sys
import time

from coop_space_contracts import captions, click_row, sp, st, wait
from coop_ui import Ui
from twoinst import Checks, start


def raids(con):
    return re.findall(r'^raid (\S+) state (\d)', sp(con, 'contracts raids'), re.M)


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    if without:
        print(' ', sp(host, 'contracts wavefix 0'))
    sp(host, 'contracts period 0')
    sp(host, 'contracts limit 0')
    hstar = re.search(r'star (\d+)', sp(host, 'star')).group(1)
    if re.search(r'star (\d+)', sp(guest, 'star')).group(1) != hstar:
        print('  guest to the host\'s star:', sp(guest, 'jumpto', hstar))
        time.sleep(12)
    uh, ug = Ui(host, 'host'), Ui(guest, 'guest')
    print('  ', sp(host, 'contracts offer armada here'))
    for c in (host, guest):
        sp(c, 'contracts accept')
    ck(wait(lambda: st(host)['state'] == 'active', 15), 'the armada contract is active')
    for ui, c in ((uh, host), (ug, guest)):
        ck(click_row(ui, c, 'Armada at', 'Land'), f'{ui.who}: a click on "Land"')
    ok = wait(lambda: sp(host, 'star').startswith('place 4') and sp(guest, 'star').split()[1] == sp(host, 'star').split()[1], 30)
    ck(ok, f'both on the colony ({sp(host, "star")} / {sp(guest, "star")})')
    planet = sp(host, 'star').split()[1]
    t0 = time.time()
    while time.time() - t0 < 60 and int(st(host)['ships_up']) == 0:
        captions(host, uh)
        captions(guest, ug)
        time.sleep(1)
    ck(int(st(host)['ships_up']) > 0, f'wave 1 came ({st(host)["ships_up"]} ships)')
    r0 = raids(host)
    print('  raids on the colony:', r0)

    # the host leaves the planet for the system view and lands again
    for _ in range(10):
        if not sp(host, 'star').startswith('place 4'):
            break
        host.call('wheel 720 540 -10')
        time.sleep(1)
    ck(not sp(host, 'star').startswith('place 4'), f'the host left the planet ({sp(host, "star")})')
    time.sleep(6)
    print('  raids while away:', raids(host))
    for _ in range(10):
        captions(host, uh)
        r = sp(host, 'land')
        if wait(lambda: sp(host, 'star').startswith('place 4'), 20):
            break
        print('  land:', r)
    ck(sp(host, 'star').split()[1] == planet, f'the host is back on the colony ({sp(host, "star")})')
    t0 = time.time()
    while time.time() - t0 < 20:
        captions(host, uh)
        time.sleep(1)
    r1 = raids(host)
    print('  raids after the return:', r1, '| host:', sp(host, 'contracts'))
    ck(len(r1) == 1, f'one raid on the colony after the return ({len(r1)}: {r1})')
    ck(st(host)['state'] == 'active', f'the contract goes on ({st(host)["state"]})')
    sp(host, 'contracts cancel')
    ck.faults(host=host, guest=guest)
    if without:
        sp(host, 'contracts wavefix 1')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
