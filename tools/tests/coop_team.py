#!/usr/bin/env python3
"""Creature co-op mechanics on two instances (docs/09-coop-design.md):
joint hunt bonus, the pack saves a player from a lethal hit, pack regeneration."""
import math
import re
import time

from coop_sync import sample
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
    # the guest's puppet has a herd of its own species: the game used to fill it with 4 clones of
    # the guest's creature (real NPCs on the host, mirrored to the guest)
    gsp = re.search(r'puppet pos=.* species=([0-9A-F]+)', host.call('coop.status')['out'])
    hsp = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    if gsp and hsp and gsp.group(1) != hsp.group(1).zfill(8):
        clones = host.call('coop.npc list')['out'].count('species=' + gsp.group(1))
        ck(clones == 0, f'no NPC clones of the guest\'s creature ({clones})')
    print(' ', host.call('coop.team')['out'].splitlines()[-1])

    # joint hunt: the guest hits, then the host hits the same NPC within 6 s
    j0, _ = team(host)
    guest.call(f'coop.npc hit {nid} 0.5')
    time.sleep(0.5)
    host.call(f'coop.npc hosthit {nid} 0.5')
    time.sleep(0.5)
    j1, _ = team(host)
    ck(j1 is not None and j0 is not None and j1 == j0 + 1, f'joint hunt bonus on the host: joint hits {j0} -> {j1}')

    # pack regeneration while the partner is close (an NPC hit would throw the guest away: set hp)
    hr0, mx0 = hp(guest)
    guest.call(f'coop.team hp {(mx0 or 10) * 0.6:.1f}')
    time.sleep(0.5)
    hr1, _ = hp(guest)
    print('  pack:', guest.call('coop.team')['out'].splitlines()[-1])
    time.sleep(4)
    hr2, _ = hp(guest)
    ck(hr2 is not None and hr1 is not None and hr2 > hr1, f'pack regen next to the partner: hp {hr1} -> {hr2}')

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

    # a guest's kill pays the pack's DNA (the game pays only kills by the host's own avatar:
    # until 2026-10-23 a guest could hunt all day for nothing; docs/re/creature.md 2)
    fam = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    fam = fam.group(1).zfill(8) if fam else ''
    mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
    prey = [m.group(1) for m in re.finditer(r'#(\d+) species=([0-9A-F]+) hp=([\d.]+)',
                                             host.call('coop.npc list')['out'])
            if m.group(1) in mirrored and m.group(2) != fam and float(m.group(3)) > 0]
    dna = lambda c: float(re.search(r'dna=([\d.]+)', c.call('coop.dna')['out']).group(1))  # noqa: E731
    d0 = dna(host)
    killed = before = None
    for cand in prey[:3]:
        before = sample(host).get('n' + cand)
        guest.call(f'coop.npc hit {cand} 100000')
        time.sleep(1.5)
        if dna(host) > d0:
            killed = 'n' + cand
            break
    d1 = dna(host)
    time.sleep(2)
    ck(d1 > d0, f'a kill by the guest pays DNA: host {d0} -> {d1} (prey {prey[:3]})')
    ck(abs(dna(guest) - d1) < 0.5, f'the guest has the same DNA ({dna(guest)})')
    # the corpse stays where the animal fell, the same on both (a forwarded hit used to push with
    # the victim's position as the impulse: the corpse flew ~80 m up, on the guest it stayed there)
    hs, gs = sample(host).get(killed), sample(guest).get(killed)
    if killed and before and hs and gs:
        moved, apart = math.dist(before[0], hs[0]), math.dist(hs[0], gs[0])
        ck(moved < 5 and apart < 3, f'the corpse lies where it fell ({moved:.1f} m) and in the same place '
                                    f'on both ({apart:.1f} m apart)')
    print('  host :', host.call('coop.team')['out'].splitlines()[-1])
    print('  guest:', guest.call('coop.team')['out'].splitlines()[-1])
    shot(host, 'team_host.png')
    shot(guest, 'team_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
