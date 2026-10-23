#!/usr/bin/env python3
"""Stage switches in running games (docs/known-issues.md history: loading a Cell save while the
Space stage ran crashed in cStar::GetSolarSystem). The host loads a fixture of another stage with
`coop.load` while the guest stays connected; the guest is then "behind" (another stage than the
host), receives the host's world and loads it in game (world.cpp). After every switch: no faults
in either game, both in the target stage and together (`together 1 1` on the host).

usage: coop_stage_switch.py [--steps space,cell,space,...]
  default route: space -> cell -> space -> creature -> space -> tribe -> civ -> cell
  (space->cell, cell->space, space->creature, creature->space, space->tribe, tribe->civ, civ->cell)
"""
import argparse
import time

import fixtures
import lane
from lane import sh
from twoinst import Checks, lane_state, start, wait_guest_in, build_id, Monitor

ROUTE = 'space,cell,space,creature,space,tribe,civ,cell'
MODE = {'cell': 0x1654C00, 'creature': 0x1654C01, 'tribe': 0x1654C02, 'civ': 0x1654C04, 'space': 0x1654C05}


def mode(con):
    try:
        return int(con.call('game.mode')['out'].split()[0], 16)
    except (OSError, ValueError, IndexError):
        return -1


def faults(con):
    try:
        return con.call('exc')['out'].splitlines()[0]
    except (OSError, ValueError, IndexError):
        return 'no answer'


def switch(host, guest, fixture, slot):
    """the host loads `fixture` from a fresh slot in game; True when both play it together"""
    t0 = time.time()
    planet = fixtures.planet_of(fixture).split('/')[1]
    sh('fixture', 'slot', fixture, lane.HOST_PROFILE, str(slot))  # never the loaded save's files
    r = host.call(f'coop.load Game{slot} {planet}')['out'].strip()
    want = MODE[fixture]
    th = None
    while time.time() - t0 < 120:
        if mode(host) == want and host.call('coop.avatar')['ok']:
            th = time.time() - t0
            break
        if Monitor.events:
            break
        time.sleep(0.3)
    # the guest is behind now: the host sends its world after 5 s and the guest loads it in game
    together = th is not None and wait_guest_in(host, guest, 150)
    hm, gm = mode(host), mode(guest)
    print(f'    {r[:50]}: host in {th and round(th, 1)} s, together={together} after {time.time() - t0:.1f} s; '
          f'host 0x{hm:08X} guest 0x{gm:08X}', flush=True)
    return together and hm == want and gm == want


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--steps', default=ROUTE)
    a = ap.parse_args()
    steps = a.steps.split(',')
    host, guest = start(steps[0])
    c = Checks()
    slot = 10
    for prev, nxt in zip(steps, steps[1:]):
        ok = switch(host, guest, nxt, slot)
        lane_state(build=build_id(), fixture=nxt, planet=fixtures.planet_of(nxt), slot=slot, warm_runs=1)
        fh, fg = faults(host), faults(guest)
        c(ok and 'total faults: 0' in fh and 'total faults: 0' in fg,
          f'{prev} -> {nxt}: both in {nxt} together, faults host "{fh}" guest "{fg}"')
        if Monitor.events or 'total faults: 0' not in fh or 'total faults: 0' not in fg:
            break  # the next switch would only add noise
        slot = slot + 1 if slot < 19 else 10
        time.sleep(3)
    c.faults(host=host, guest=guest)
    return c.result()


if __name__ == '__main__':
    raise SystemExit(main())
