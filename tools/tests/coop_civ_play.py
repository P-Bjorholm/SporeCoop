#!/usr/bin/env python3
"""Civilization co-op mechanics through the game's own functions (not a real battle):
joint siege (capture damage x1.5 when two players attack a city), the alarm for an attacked
player city (everybody is told, beacons), the superweapon fired by a vote."""
import re
import time

from coop_civ import civ
from coop_ui import Ui
from twoinst import Checks, shot, start


def main():
    host, guest = start('civ', 'Game0/Agrauel')
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    npc = [k for k, v in hc.items() if not v['player']]
    mine = [k for k, v in hc.items() if v['player']]
    print('  cities: player', mine, 'npc', npc[:6])
    # an NPC city if the planet has one in reach, else the player city (the capture math is the same)
    target = npc[0] if npc else (mine[0] if mine else None)
    if target is not None:
        c = target
        r1 = host.call(f'coop.civ siege {c} 0')['out']
        r2 = host.call(f'coop.civ siege {c} 1')['out']
        print('  host hit:', r1, '| guest hit:', r2)
        d = [float(b) - float(a) for a, b in re.findall(r'capture ([\d.]+) -> ([\d.]+)', r1 + ' ' + r2)]
        ck(len(d) == 2 and abs(d[0] - 1.0) < 0.05 and abs(d[1] - 1.5) < 0.05,
           f'a city attacked by two players falls 50% faster (capture +{d})')
        ck('joint siege' in host.call('coop.status')['out'], 'everybody is told about the joint siege')
    if mine:
        print(' ', host.call(f'coop.civ alarm {mine[0]}')['out'])
        time.sleep(2)
        ck('under attack: help' in guest.call('coop.status')['out'], 'the guest is told the player city is attacked')
        shot(guest, 'civ_alarm_guest.png')
    # the superweapon waits for a vote
    print(' ', host.call('coop.civ super 0')['out'])
    time.sleep(2)
    ck('vote active' in guest.call('coop.vote')['out'], 'a superweapon starts a vote')
    gu = Ui(guest, 'guest')
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    time.sleep(3)
    st = host.call('coop.status')['out']
    ck('the superweapon fires' in st, 'it fires when everybody is ready')
    print(' ', st.split('civ(host)')[1].splitlines()[0])
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
