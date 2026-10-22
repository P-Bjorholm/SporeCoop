#!/usr/bin/env python3
"""Creature co-op mechanics on two instances (docs/09-coop-design.md):
joint hunt bonus, the pack saves a player from a lethal hit, pack regeneration."""
import re
import time

from twoinst import Checks, shot, start


def team(con):
    out = con.call('coop.team')['out']
    m = re.search(r'joint hits (\d+), rescues (\d+)', out)
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def hp(con):
    m = re.search(r'hp=([\d.]+)/([\d.]+)', con.call('coop.avatar')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else (None, None)


def main():
    host, guest = start()
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    # an NPC the host simulates and the guest mirrors
    nid = None
    for _ in range(20):
        mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
        for line in host.call('coop.npc list')['out'].splitlines():
            m = re.match(r'\s*#(\d+) .*hp=([\d.]+) dist=([\d.]+)', line)
            if m and m.group(1) in mirrored and float(m.group(2)) > 5 and 'DEAD' not in line:
                nid = m.group(1)
                break
        if nid:
            break
        time.sleep(1)
    ck(nid is not None, f'guest mirrors host NPCs (target #{nid})')
    print(' ', host.call('coop.team')['out'].splitlines()[-1])

    # joint hunt: the guest hits, then the host hits the same NPC within 6 s
    j0, _ = team(host)
    guest.call(f'coop.npc hit {nid} 0.5')
    time.sleep(0.5)
    host.call(f'coop.npc hosthit {nid} 0.5')
    time.sleep(0.5)
    j1, _ = team(host)
    ck(j1 is not None and j0 is not None and j1 == j0 + 1, f'joint hunt bonus on the host: joint hits {j0} -> {j1}')

    # the pack protects: the host forwards a lethal NPC hit to the guest standing next to it
    h0, mx = hp(guest)
    _, r0 = team(guest)
    def damage_in():
        m = re.search(r'damage_in=(\d+)', guest.call('coop.npc')['out'])
        return int(m.group(1)) if m else 0
    d0 = damage_in()
    ids = re.findall(r'#(\d+) .*hp=', host.call('coop.npc list')['out'])
    for cand in [nid] + ids:
        out = host.call(f'coop.npc hurt 1 {cand} 100000')['out']
        time.sleep(1.5)
        if 'puppet hit' in out and damage_in() > d0:
            break
    h1, _ = hp(guest)
    _, r1 = team(guest)
    ck(h1 is not None and h1 > 0, f'guest survived a lethal hit: hp {h0} -> {h1} of {mx}')
    ck(r1 == (r0 or 0) + 1, f'rescue counted ({r0} -> {r1})')
    chat = host.call('coop.status')['out']
    ck('saved Bob' in chat, 'everybody sees "Alice saved Bob from death"')

    # pack regeneration while the partner is close (a lethal hit can throw the guest away)
    print(' ', guest.call('coop.team come')['out'])
    time.sleep(4)
    h2, _ = hp(guest)
    ck(h2 is not None and h1 is not None and h2 > h1, f'pack regen: hp {h1} -> {h2}')
    print('  host :', host.call('coop.team')['out'].splitlines()[-1])
    print('  guest:', guest.call('coop.team')['out'].splitlines()[-1])
    shot(host, 'team_host.png')
    shot(guest, 'team_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
