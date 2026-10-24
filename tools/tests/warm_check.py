#!/usr/bin/env python3
"""Is a warm reset (fixture files restored on disk + coop.load in the running game) a clean
start? Changes the tribe food, reloads, checks it is back; then switches stage fixtures
(tribe -> civ -> space -> creature -> cell) without restarting the games; then a mod hot reload."""
import re
import time

import lane
from fixtures import planet_of
from lane import sh
from twoinst import start, wait_guest_in

FIX = {fx: planet_of(fx) for fx in ('tribe', 'civ', 'space', 'cell')}
FIX['creature'] = None


def mode(con):
    return con.call('game.mode')['out'].strip().split('\n')[0]


SLOT = [10]


def reload(host, guest, fixture, planet):
    t0 = time.time()
    guest.call('coop.leave')
    SLOT[0] = SLOT[0] + 1 if SLOT[0] < 19 else 10  # never touch the loaded save
    sh('fixture', 'slot', fixture, lane.HOST_PROFILE, str(SLOT[0]))
    name = planet.split('/')[1]
    r = host.call(f'coop.load Game{SLOT[0]} {name}')['out']
    seen, last = [], None
    while time.time() - t0 < 120:
        m = mode(host)
        if m != last:
            seen.append(f'{time.time() - t0:.1f} {m[11:]}')
            last = m
        if len(seen) > 1 and 'Load' not in m and 'Menu' not in m and host.call('coop.avatar')['ok']:
            break
        time.sleep(0.3)
    th = time.time() - t0
    host.call(lane.HOST_CMD)
    guest.call(lane.JOIN)
    ok = wait_guest_in(host, guest, 120)
    print(f'  {fixture}: host {th:.1f} s, guest in={ok} after {time.time() - t0:.1f} s  {r.strip()[:60]} {seen}')
    return ok


def food(con):
    m = re.search(r'food[= ]+(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def main():
    host, guest = start('tribe', FIX['tribe'])
    f0 = food(host)
    print(' ', host.call('coop.res add food 50')['out'].strip())
    f1 = food(host)
    reload(host, guest, 'tribe', FIX['tribe'])
    f2 = food(host)
    print(f'  food: fixture {f0} -> changed {f1} -> after the warm reset {f2}  {"CLEAN" if f2 == f0 else "DIRTY"}')
    for fx in ('civ', 'space', 'cell'):
        reload(host, guest, fx, FIX[fx])
        print('    host', mode(host), '| guest', mode(guest), '| faults', host.call('exc')['out'].splitlines()[0])
    # hot reload of the mod in both games
    t0 = time.time()
    for c in (host, guest):
        print('  mod.reload:', c.call('mod.reload coop')['out'].strip()[:100])
    time.sleep(2)
    print(f'  after hot reload ({time.time() - t0:.1f} s):', host.call('coop.status')['out'].splitlines()[0][:80])
    host.call(lane.HOST_CMD)
    guest.call(lane.JOIN)
    print('  rejoin after reload:', wait_guest_in(host, guest, 120), guest.call('coop.status')['out'].splitlines()[0][:80])
    print('  faults host', host.call('exc')['out'].splitlines()[0], '| guest', guest.call('exc')['out'].splitlines()[0])


if __name__ == '__main__':
    main()
