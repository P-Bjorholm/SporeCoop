#!/usr/bin/env python3
"""How exactly the guest shows what the host has, in every stage: position and facing of every
synchronized object (players, NPC creatures/cells, citizens, vehicles, ships), host vs guest.

For each stage: start host + guest, let things move (the host swims/walks, vehicles drive, ships
fly), then take rounds of host / guest / host samples (coop.sync) and compare by key. A guest sample
is compared with the closer of the two host samples around it, so the console round trip does not
count as lag. Prints per kind: count, median / 90% / max position error (m) and facing error (deg).

usage: coop_sync.py [stage ...]   (default: cell creature tribe civ space)
"""
import math
import re
import sys
import time

from twoinst import Checks, shot, start

FIXTURES = {
    'cell': ('cell', 'Game0/Karke'),
    'creature': ('creature', None),
    'tribe': ('tribe', 'Game0/Cohrach'),
    'civ': ('civ', 'Game0/Agrauel'),
    'space': ('space', 'Game0/Maraya'),
}
KINDS = {'p': 'players', 'n': 'NPC creatures', 'c': 'NPC cells', 't': 'citizens', 'v': 'vehicles', 's': 'ships'}
# what counts as "in sync": position (m, relative to the kind) and facing (deg)
LIMITS = {'p': (1.5, 15), 'n': (1.5, 25), 'c': (0.6, 20), 't': (1.5, 30), 'v': (3.0, 20), 's': (10.0, 20)}  # ships fly ~100 m/s


def sample(con):
    d = {}
    for line in con.call('coop.sync')['out'].splitlines():
        f = line.split()
        if len(f) == 8:
            try:
                d[f[0]] = (tuple(map(float, f[1:4])), tuple(map(float, f[4:8])))
            except ValueError:
                pass
    return d


def angle(q1, q2):
    dot = abs(sum(a * b for a, b in zip(q1, q2)))
    n = math.sqrt(sum(a * a for a in q1)) * math.sqrt(sum(b * b for b in q2)) or 1
    return math.degrees(2 * math.acos(min(1.0, dot / n)))


def pct(v, p):
    if not v:
        return float('nan')
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p))]


def civ_state(con):
    cities, veh = [], {}
    for line in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'city .* pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m:
            cities.append(tuple(float(m.group(i)) for i in (1, 2, 3)))
        m = re.match(r'\s+veh #(\d+) .* pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and m.group(1) != '0':
            veh[int(m.group(1))] = tuple(float(m.group(i)) for i in (2, 3, 4))
    return cities, veh


def stir(stage, host, guest, i):
    """keep things moving between rounds"""
    if stage == 'space' and i == 0:
        for _ in range(3):
            host.call('coop.fleet spawn 2')
    if stage == 'civ':  # drive the vehicles 40 m away from / back towards the nearest city
        cities, veh = civ_state(host)
        for vid, p in list(veh.items())[:4]:
            if not cities:
                break
            c = min(cities, key=lambda c: math.dist(c, p))
            d = math.dist(p, c) or 1
            k = (d + (40 if i % 2 == 0 else -25)) / d
            tgt = tuple(cc + (pp - cc) * k for pp, cc in zip(p, c))
            host.call('coop.civ move %d %.1f %.1f %.1f' % ((vid,) + tgt))
    if stage == 'cell':
        host.call('coop.pool goto %d %d' % ([20, 0, -20, 0][i % 4], [0, 20, 0, -20][i % 4]))


def measure(stage):
    fx, planet = FIXTURES[stage]
    host, guest = start(fx, planet)
    time.sleep(5)
    pos, rot, keys = {}, {}, {}
    for i in range(16):
        if i % 4 == 0:
            stir(stage, host, guest, i // 4)
        h1, g, h2 = sample(host), sample(guest), sample(host)
        for k, (gp, gq) in g.items():
            if k not in h1 or k not in h2:
                continue
            kind = k[0]
            e1, e2 = math.dist(gp, h1[k][0]), math.dist(gp, h2[k][0])
            hq = h1[k][1] if e1 <= e2 else h2[k][1]
            pos.setdefault(kind, []).append(min(e1, e2))
            rot.setdefault(kind, []).append(angle(gq, hq))
            keys.setdefault(kind, set()).add(k)
        time.sleep(0.5)
    shot(host, f'sync_{stage}_host.png')
    shot(guest, f'sync_{stage}_guest.png')
    return pos, rot, keys, host, guest


def main():
    stages = sys.argv[1:] or ['cell', 'creature', 'tribe', 'civ', 'space']
    ck = Checks()
    for st in stages:
        print(f'== {st}', flush=True)
        pos, rot, keys, host, guest = measure(st)
        if not pos:
            print('  nothing in common: host', list(sample(host))[:6], 'guest', list(sample(guest))[:6])
        for kind in sorted(pos):
            p, r = pos[kind], rot[kind]
            lp, lr = LIMITS.get(kind, (2, 20))
            print(f'  {KINDS.get(kind, kind):14} n={len(keys[kind]):3}  pos med {pct(p, .5):5.2f} p90 {pct(p, .9):5.2f} '
                  f'max {max(p):6.2f} m | facing med {pct(r, .5):5.1f} p90 {pct(r, .9):5.1f} max {max(r):5.1f} deg',
                  flush=True)
            ck(pct(p, .9) <= lp, f'{st}/{KINDS.get(kind, kind)}: 90% within {lp} m')
            ck(pct(r, .9) <= lr, f'{st}/{KINDS.get(kind, kind)}: 90% facing within {lr} deg')
        ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
