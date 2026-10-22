#!/usr/bin/env python3
"""Space co-op ("Squadron") on two instances in the same star system: the host's NPC ships are
mirrored on the guest, guest shots are applied by the host, NPC hits on the guest's ship reach
the guest."""
import re
import time

from twoinst import Checks, shot, start


def ships(con):
    out = con.call('coop.fleet list')['out']
    d = {}
    for m in re.finditer(r'ship #(-?\d+) \S+ type=(\d+) pol=\w+ hp=([\d.]+)/([\d.]+)( \(me\))?', out):
        d.setdefault('me' if m.group(5) else int(m.group(1)), []).append((int(m.group(2)), float(m.group(3))))
    return d, out


def num(con, key):
    m = re.search(key + r'=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space', 'Game0/Maraya')
    ck = Checks()
    time.sleep(3)
    print(' ', host.call('coop.avatar')['out'].splitlines()[0])
    print(' ', guest.call('coop.avatar')['out'].splitlines()[0])
    print(' ', host.call('coop.fleet spawn 2')['out'])
    time.sleep(3)
    hs, ho = ships(host)
    gs, go = ships(guest)
    print(ho)
    print(go)
    npc = [k for k, v in hs.items() if k != 'me' and k >= 0 and v[0][0] == 2]
    ck(npc, f'the host has an NPC ship {npc}')
    if not npc:
        return ck.result()
    sid = npc[0]
    ck(sid in gs, f'the guest mirrors ship #{sid}')
    hp0 = hs[sid][0][1]
    print(' ', guest.call(f'coop.fleet shoot {sid} 5')['out'])
    time.sleep(1.5)
    hs2, _ = ships(host)
    ck(num(host, 'hits_in') >= 1, 'the host applied the guest shot')
    ck(sid in hs2 and hs2[sid][0][1] < hp0, f'ship #{sid} hp {hp0} -> {hs2.get(sid, [(0, -1)])[0][1]}')
    d0 = num(guest, 'damage_in')
    print(' ', host.call(f'coop.fleet hurt {sid} 1')['out'])
    time.sleep(1.5)
    ck(num(guest, 'damage_in') > d0, 'the NPC hit reached the guest ship')
    shot(host, 'space_host.png')
    shot(guest, 'space_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
