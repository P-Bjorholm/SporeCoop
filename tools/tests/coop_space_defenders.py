#!/usr/bin/env python3
"""Space: the game's own Defenders are not taken for the partners' puppets (BUG-0111).

The mod shows a partner as a Defender UFO (UfoType 6); the game makes Defenders too (an NPC
empire's system, a colony's defence). Any type 6 counted as "our puppet":
1. the guest kept its own game's Defenders in the host's place while the host's came as mirrors:
   each defender twice. Here the guest's game spawns a Defender of its empire next to it
   (`coop.fleet spawn 6 own`, SpawnUFO as the game's colony defence does); the guest must remove
   it like its other NPC ships, and both games show as many Defenders in that place;
2. a game Defender made at a freed puppet's address was adopted as the partner's ship (driven,
   recoloured, destroyed). Forced: `coop.fleet impostor 1` gives the guest's puppet on the host
   another model key, as such a ship has; the host must leave that ship alone and show the guest
   with a new puppet.

  SPORE_LANE=6 python3 tools/tests/coop_space_defenders.py [--without-fix]
"""
import re
import sys
import time

from twoinst import Checks, start


def fleet(con):
    """{pointer: (id, type)} of the game's UFO list"""
    out = con.call('coop.fleet list')['out']
    return {m.group(2): (int(m.group(1)), int(m.group(3)))
            for m in re.finditer(r'ship #(-?\d+) (\S+) type=(\d+) pol=\w+ hp=', out)}


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if without:
        for c in (host, guest):
            print(' ', c.call('coop.fleet typefix 0')['out'].strip())

    # ---- 1. a Defender of the guest's own game in the host's place ----
    r = guest.call('coop.fleet spawn 6 own')['out'].strip()
    print('  guest:', r)
    m = re.search(r'spawned (\S+) type=6', r)
    ck(m, 'the guest\'s game made a Defender')
    print('  host:', host.call('coop.fleet spawn 6 own')['out'].strip())
    time.sleep(5)
    hf, gf = fleet(host), fleet(guest)
    h6 = sorted(p for p, (i, t) in hf.items() if t == 6)
    g6 = sorted(p for p, (i, t) in gf.items() if t == 6)
    print(f'  type 6 ships: host {len(h6)} {h6}, guest {len(g6)} {g6}')
    if m:
        ck(m.group(1) not in gf, f'the guest removed its own game\'s Defender {m.group(1)} (the host\'s are mirrored)')
    ck(len(g6) == len(h6), f'both games show as many Defenders here (host {len(h6)}, guest {len(g6)})')

    # ---- 2. a game Defender at the puppet's address is not the guest's ship ----
    r = host.call('coop.fleet impostor 1')['out'].strip()
    print('  host:', r)
    m = re.search(r'puppet (\S+) type=6', r)
    ck(m, 'the host shows the guest as a Defender puppet')
    if m:
        q = m.group(1)
        time.sleep(3)
        r = host.call('coop.fleet puppet 1')['out'].strip()
        print('  host:', r)
        p = re.search(r'puppet (\S+) type=6', r)
        hf = fleet(host)
        ck(p and p.group(1) != q, f'the host shows the guest with a new puppet ({q} -> {p.group(1) if p else None})')
        ck(q in hf, f'the ship at the old address is left to the game, not destroyed ({q} listed: {q in hf})')
    ck.faults(host=host, guest=guest)
    if without:
        for c in (host, guest):
            c.call('coop.fleet typefix 1')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
