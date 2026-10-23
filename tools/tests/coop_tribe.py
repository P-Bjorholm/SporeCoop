#!/usr/bin/env python3
"""Tribe stage co-op ("Chiefs") on two instances: the host simulates every tribe, the guest
mirrors them; the player tribe is split between the players; a player selects and orders only
their own members; guest orders go through the host."""
import math
import re
import time

from twoinst import Checks, shot, start


def members(con):
    """{id: (owner, sel, (x, y, z), tribe)}"""
    out, tribe = {}, None
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)', line)
        if m:
            tribe = int(m.group(1)) if 'player' not in line else 'player'
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) age=\d+ sel=(\d+) hp=[\d.]+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and m.group(1) != '0':
            out[int(m.group(1))] = (int(m.group(2)), int(m.group(3)),
                                    tuple(float(m.group(i)) for i in (4, 5, 6)), tribe)
    return out


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def main():
    host, guest = start('tribe', 'Game0/Cohrach')
    ck = Checks()
    t0 = time.time()
    while True:  # the guest may still be finishing the load of the host world
        hm, gm = members(host), members(guest)
        if (hm and set(hm) == set(gm)) or time.time() - t0 > 60:
            break
        time.sleep(2)
    if not gm:
        print('  guest state:', guest.call('game.mode')['out'], guest.call('coop.avatar')['out'][:200])
        print(guest.call('coop.tribe')['out'][:800])
    print('  host :', {k: (v[0], v[3]) for k, v in hm.items()})
    print('  guest:', {k: (v[0], v[3]) for k, v in gm.items()})
    ck(len(hm) > 0 and set(hm) == set(gm), f'guest mirrors every citizen ({len(hm)} host, {len(gm)} guest)')
    # walking citizens move between the two samples: take the best of a few rounds
    errs = []
    for _ in range(4):
        h1, g1, h2 = members(host), members(guest), members(host)
        errs.append(max((min(dist(h1[i][2], g1[i][2]), dist(h2[i][2], g1[i][2]))
                         for i in h1 if i in g1 and i in h2), default=99))
        time.sleep(1)
    err = min(errs)
    ck(err < 3, f'mirrors are where the host has them (max error {err:.2f} m; rounds {[round(e, 1) for e in errs]})')
    mine = [i for i, v in gm.items() if v[0] == 1 and v[3] == 'player']
    theirs = [i for i, v in gm.items() if v[0] == 0 and v[3] == 'player']
    ck(mine and theirs, f'player tribe split: guest owns {mine}, host owns {theirs}')
    if not (mine and theirs):
        return ck.result()

    # selection: only own members
    r = guest.call(f'coop.tribe select {theirs[0]} 1')['out']
    ck('selected=0' in r, f'guest cannot select a host member ({r})')
    r = host.call(f'coop.tribe select {mine[0]} 1')['out']
    ck('selected=0' in r, f'host cannot select a guest member ({r})')
    r = guest.call(f'coop.tribe select {mine[0]} 1')['out']
    ck('selected=1' in r, f'guest selects its own member ({r})')

    # walk order: guest -> host -> the member walks on both machines
    start_pos = hm[mine[0]][2]
    others = [v[2] for i, v in hm.items() if v[3] == 'player' and i != mine[0]]
    dest = others[0] if others else start_pos
    dest = tuple(a + (b - a) * 0.6 for a, b in zip(start_pos, dest))
    print('  walk', start_pos, '->', dest)
    print(' ', guest.call('coop.tribe walk %.2f %.2f %.2f x' % dest)['out'])
    time.sleep(8)
    hm2, gm2 = members(host), members(guest)
    moved = dist(hm2[mine[0]][2], start_pos)
    ck(moved > 1.5, f'the host walked the guest member {moved:.1f} m')
    ck(dist(hm2[mine[0]][2], gm2[mine[0]][2]) < 3, 'the guest sees the walk')
    st = host.call('coop.status')['out']
    ck(re.search(r'cmds_done=[1-9]', st), 'host executed the order: ' + re.search(r'tribe\(host\).*', st).group(0))

    # action on an object: attack a member of the other tribe (CitizenAction 7)
    npcs = [i for i, v in hm2.items() if v[3] != 'player']
    if npcs:
        print(' ', guest.call(f'coop.tribe act 7 {npcs[0]}')['out'])
        time.sleep(2)
        st = host.call('coop.status')['out']
        m = re.search(r'cmds_done=(\d+)', st)
        ck(m and int(m.group(1)) >= 2, 'host executed the action order: ' + re.search(r'tribe\(host\).*', st).group(0))
    # animals are shared too: hunt (CitizenAction 10) a mirrored animal
    gl = guest.call('coop.npc list')['out']
    animals = re.findall(r'#(\d+) .*spawned hp=([\d.]+)', gl)
    ck(len(animals) > 0, f'guest mirrors the host animals ({len(animals)})')
    if animals:
        m0 = re.search(r'cmds_done=(\d+)', host.call('coop.status')['out'])
        print(' ', guest.call(f'coop.tribe act_npc 10 {animals[0][0]}')['out'])
        time.sleep(2)
        st = host.call('coop.status')['out']
        m1 = re.search(r'cmds_done=(\d+)', st)
        ck(m0 and m1 and int(m1.group(1)) > int(m0.group(1)),
           'host found the animal and executed the hunt: ' + re.search(r'tribe\(host\).*', st).group(0))
    # a gift: the guest gives one of its selected members to the host
    gm = members(guest)
    mine = [i for i, v in gm.items() if v[0] == 1 and v[3] == 'player']
    if mine:
        for i in gm:
            guest.call(f'coop.tribe select {i} 0')
        guest.call(f'coop.tribe select {mine[0]} 1')
        print(' ', guest.call('coop.give')['out'])
        time.sleep(2)
        hm = members(host)
        ck(hm.get(mine[0], (None,))[0] == 0, f'the host owns the gifted member #{mine[0]} now')
        time.sleep(1)
        ck(members(guest).get(mine[0], (None,))[0] == 0, 'the guest sees the new owner')
        # the giver must not keep the gifted member selected (it could order the partner's member)
        ck(members(guest).get(mine[0], (0, 1))[1] == 0, 'the gifted member is no longer selected by the giver')
        ck('gave 1 tribe member' in host.call('coop.status')['out'], 'everybody is told about the gift')
    shot(host, 'tribe_host.png')
    shot(guest, 'tribe_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
