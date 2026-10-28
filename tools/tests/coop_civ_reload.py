#!/usr/bin/env python3
"""Civilization: the host's tables of game pointers across a load and a leave (BUG-0040, BUG-0059).

1. Two games play the civ_npc fixture; the host raises the alarm for its capital (`coop.civ
   alarm`): the vehicles near it are boosted (`coop.civ tables`: boost > 0).
2. The host loads the same planet again (a fresh slot) while connected: every old vehicle and city
   is freed. Afterwards no table may still hold the old world: no boost, no alarm time, no vehicle
   that kept its old address AND its old net id (an inherited owner), no vehicle of another nation
   with a player owner (pol of the game's own vehicle against the owner the mod gives it).
3. The host raises the alarm again and leaves the session: the boost and alarm tables are empty
   (before the fix the boost map survived the session and wrote into freed vehicles).

--nofix: the code before the fixes (`coop.civ fix boost 0`, `coop.civ fix world 0`): must FAIL.

  SPORE_LANE=5 python3 tools/tests/coop_civ_reload.py [--nofix]
"""
import argparse
import re
import time

import fixtures
import lane
from lane import sh
from twoinst import Checks, Monitor, build_id, lane_state, start, wait_guest_in

CIV = 0x1654C04


def mode(con):
    try:
        return int(con.call('game.mode')['out'].split()[0], 16)
    except (OSError, ValueError, IndexError):
        return -1


def tables(con):
    out = con.call('coop.civ tables')['out']
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', out)}, out.strip()


def listing(con):
    """player pol, [(addr, id, pol, owner)] of the game's vehicles as the mod sees them"""
    ppol, veh = None, []
    for line in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'city -?\d+ \S+ pol=(\w+) \(player\)', line)
        if m:
            ppol = m.group(1)
        m = re.match(r'\s+veh #(\d+) (\S+) pol=(\w+) owner=(-?\d+)', line)
        if m:
            veh.append((m.group(2), int(m.group(1)), m.group(3), int(m.group(4))))
    return ppol, veh


def player_cities(con):
    return [int(m.group(1)) for m in re.finditer(r'^city (\d+) \S+ pol=\w+ \(player\)', con.call('coop.civ')['out'], re.M)]


def reload(host, guest, slot):
    """the host loads civ_npc again from a fresh slot while connected; True when back in Civ"""
    planet = fixtures.planet_of('civ_npc').split('/')[1]
    sh('fixture', 'slot', 'civ_npc', lane.HOST_PROFILE, str(slot))  # never the loaded save's files
    print('  ', host.call(f'coop.load Game{slot} {planet}')['out'].strip()[:60], flush=True)
    t0, left = time.time(), False
    while time.time() - t0 < 150 and not Monitor.events:
        m = mode(host)
        left |= m != CIV or not host.call('coop.avatar')['ok']
        if left and m == CIV and host.call('coop.avatar')['ok']:
            break
        f = host.call('frame')['out']
        time.sleep(0.7)
        if host.call('frame')['out'] == f:
            host.call('key enter')  # a box that stops the frames ("game saved")
    else:
        return False
    print(f'   host back in Civ after {time.time() - t0:.0f} s', flush=True)
    return wait_guest_in(host, guest, 120)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.civ fix boost 0')['out'].strip(), '|', c.call('coop.civ fix world 0')['out'].strip())
    time.sleep(4)
    mine = player_cities(host)
    ck(mine, f'the host has player cities: {mine}')
    if not mine:
        return ck.result()
    print(' ', host.call(f'coop.civ alarm {mine[0]}')['out'].strip())
    time.sleep(1)
    t0, txt = tables(host)
    print('  before:', txt)
    ck(t0.get('boost', 0) > 0 and t0.get('alarm_at', 0) > 0, 'the alarm boosted vehicles near the capital')
    _, before = listing(host)
    old = {(addr, vid) for addr, vid, _, _ in before}

    st = lane_state()
    slot = 10 + (st.get('slot', 9) - 9) % 10
    ok = reload(host, guest, slot)
    lane_state(**{**st, 'build': build_id(), 'fixture': 'civ_npc', 'planet': fixtures.planet_of('civ_npc'),
                  'slot': slot})
    ck(ok, 'the host loaded the planet again and the guest is with it')
    time.sleep(3)
    t1, txt = tables(host)
    print('  after the load:', txt)
    ck(t1.get('alarm_at', 1) == 0 and t1.get('boost', 1) == 0,
       'no boost or alarm of the old world is left after the load')
    ppol, after = listing(host)
    kept = [(addr, vid) for addr, vid, _, _ in after if (addr, vid) in old]
    print(f'  vehicles before {len(before)}, after {len(after)}; same address and id: {len(kept)} {kept[:6]}')
    ck(not kept, 'no vehicle of the new world kept an id (and owner) of the old one')
    wrong = [(addr, vid, pol, o) for addr, vid, pol, o in after if ppol and pol != ppol and o not in (254, -1)]
    ck(not wrong, f'no vehicle of another nation has a player owner ({wrong[:6]})')

    # an alarm, then the host leaves: the boost table goes with the session
    mine = player_cities(host)
    if mine:
        print(' ', host.call(f'coop.civ alarm {mine[0]}')['out'].strip())
        time.sleep(1)
        t2, txt = tables(host)
        print('  alarm again:', txt)
        host.call('coop.leave')
        time.sleep(2)
        t3, txt = tables(host)
        print('  after the leave:', txt)
        ck(t2.get('boost', 0) == 0 or t3.get('boost', 1) == 0, 'the boosts end with the session')
        ck(t3.get('alarm_at', 1) == 0 and t3.get('hv', 1) == 0, 'the alarm and vehicle tables end with the session')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
