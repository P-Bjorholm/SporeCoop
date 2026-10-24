#!/usr/bin/env python3
"""Space: the partners' ships (Defender UFO puppets) and the guest's mirrors of the host's ships
survive the game freeing them. Landing on a planet or taking off frees every NPC UFO of the place
(DestroyNPCUFOs), the mod's puppets too; the mod kept driving the freed puppet and the host crashed
(AV in modes::space_drive, play_space.py 2026-10-23; docs/stability/space.md).

Both players start in the same star system. Four rounds: both land on the same planet with real
input (click on the planet, mouse wheel), then one of them takes off (in turns) while the other
stays, then the other follows. Checks after every step: both games draw and answer, no
fault, and back in the system each sees the other's ship again. At the end the counter of puppets
and mirrors found freed by the game (coop.status freed_by_game) shows the path was taken."""
import re
import time

import fixtures
import lane
from play_space import Player
from twoinst import Checks, Monitor, shot, start


def together(con, pid):
    return re.search(rf'together {pid} 1', con.call('coop.status')['out']) is not None


def freed(con):
    m = re.search(r'freed_by_game=(\d+)', con.call('coop.fleet list')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    end = time.time() + 900
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, end, stats)
    pg = Player('guest', lane.GUEST, 1, 2, end, stats)
    ph.partner, pg.partner = pg, ph
    for p in (ph, pg):
        p.last_place = p.place()
    ck(ph.ctx() == 'system' and pg.ctx() == 'system', 'both start in the star system')
    host.call('coop.fleet spawn 2')
    t0 = time.time()
    while time.time() - t0 < 20 and not (together(host, 1) and together(guest, 0)):
        time.sleep(1)
    ck(together(host, 1) and together(guest, 0), 'both see the partner ship')
    # one planet of the home system per round (fixed, not random: the run is the same every time);
    # Vulpica (P02460035) is the first mission's planet: landing there shows tutorial captions that
    # take the input until OK (Player.take_off confirms them; before that, rounds 2-4 failed whenever
    # the random pick was Vulpica)
    planets = ['01460035', '02460035', '03460035', fixtures.FIXTURES['space']['home']]
    for rnd in range(4):
        # both land on the same planet (the host picks one, the guest follows), then one takes off:
        # leaving the planet frees its UFOs, the partner's puppet too (the crash of 2026-10-23)
        ph.land_retry(planets[rnd])
        planet = ph.place()[0]
        pg.land_retry('%08X' % (planet & 0x3FFFFFFF))
        t0 = time.time()
        while time.time() - t0 < 20 and not (together(host, 1) and together(guest, 0)):
            time.sleep(1)
        ck(planet & 0x40000000 and pg.place()[0] == planet and together(host, 1) and together(guest, 0),
           f'round {rnd + 1}: both on planet {planet:08X} and see each other')
        if rnd == 0:
            # nobody shoots: a pirate next to both must keep its health (the guest's ship on the host is
            # a Defender UFO whose own AI used to shoot every hostile ship in range)
            host.call('coop.fleet spawn 2')
            time.sleep(20)
            out = host.call('coop.fleet list')['out']
            hp = [(float(a), float(b)) for a, b in re.findall(r'type=2 pol=\w+ hp=([\d.]+)/([\d.]+)', out)]
            blocked = re.search(r'puppet_shots_blocked=(\d+)', out)
            print('  ', out.splitlines()[0], hp)
            ck(hp and all(a == b for a, b in hp), f'a pirate nobody shoots keeps its health {hp}')
        first, second = (ph, pg) if rnd % 2 == 0 else (pg, ph)
        f0 = freed(host) + freed(guest)
        ck(first.take_off(), f'round {rnd + 1}: the {first.name} took off, the {second.name} stays')
        time.sleep(4)
        ck(not Monitor.events and freed(host) + freed(guest) > f0,
           f'round {rnd + 1}: the game freed the puppet, both still draw (freed by the game: host {freed(host)}, '
           f'guest {freed(guest)})')
        ck(second.take_off(), f'round {rnd + 1}: the {second.name} took off too')
        t0 = time.time()
        while time.time() - t0 < 25 and not (together(host, 1) and together(guest, 0)):
            time.sleep(1)
        ck(together(host, 1) and together(guest, 0) and not Monitor.events,
           f'round {rnd + 1}: in the system both see each other again')
        if Monitor.events:
            break
    ck(freed(host) + freed(guest) > 0, 'the game freed puppets or mirrors and the mod let them go')
    shot(host, 'space_puppet_host.png')
    shot(guest, 'space_puppet_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
