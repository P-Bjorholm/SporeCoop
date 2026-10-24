#!/usr/bin/env python3
"""Where the time of a two-instance test goes: start phases (printed by twoinst.start), memory,
then the cost of a reset without restarting the games: the host reloads the fixture planet
in game, the guest leaves and joins again."""
import re
import subprocess
import sys
import time

import lane
from fixtures import planet_of
from twoinst import start, wait_guest_in


def rss_mb():
    out = subprocess.run(['ps', '-eo', 'rss,args'], capture_output=True, text=True).stdout
    return [int(l.split()[0]) // 1024 for l in out.splitlines() if re.search(r'[A-Z]:\\.*SporeApp\.exe', l)]


def main():
    fixture, planet = (sys.argv[1], sys.argv[2]) if len(sys.argv) > 2 else ('tribe', planet_of('tribe'))
    host, guest = start(fixture, planet)
    print('  RSS MB per SporeApp:', rss_mb())
    def watch(con, until, timeout=240):
        """game.mode transitions with times until until(mode) holds after a change"""
        t0, last, seen = time.time(), None, []
        while time.time() - t0 < timeout:
            m = con.call('game.mode')['out'].strip().split('\n')[0][:60]
            if m != last:
                seen.append(f'{time.time() - t0:.1f}s {m}')
                last = m
            if len(seen) > 1 and until(m):
                break
            time.sleep(0.3)
        return time.time() - t0, seen

    slot, name = planet.split('/')
    print('  mode now:', host.call('game.mode')['out'].strip()[:80])
    # reset 1: the guest leaves, the host reloads its planet while both games keep running
    guest.call('coop.leave')
    t0 = time.time()
    host.call(f'coop.load {slot} {name}')
    dt, seen = watch(host, lambda m: 'Load' not in m and 'Menu' not in m)
    print(f'  host reload in game: {dt:.1f} s', seen)
    host.call('wait.frames 30 20000')
    t1 = time.time()
    guest.call(lane.JOIN)
    dt, seen = watch(guest, lambda m: 'Load' not in m and 'Menu' not in m and 'Galaxy' not in m)
    wait_guest_in(host, guest)
    print(f'  guest join + load: {time.time() - t1:.1f} s', seen)
    print(f'  warm reset total: {time.time() - t0:.1f} s')
    # reset 2: only the guest rejoins (same world, already on disk)
    guest.call('coop.leave')
    time.sleep(2)
    t0 = time.time()
    guest.call(lane.JOIN)
    dt, seen = watch(guest, lambda m: 'Load' not in m and 'Menu' not in m and 'Galaxy' not in m, 120)
    print(f'  guest rejoin: {time.time() - t0:.1f} s', seen)
    print('  guest status:', guest.call('coop.status')['out'].splitlines()[0][:150])
    print('  RSS MB per SporeApp:', rss_mb())
    print(' ', host.call('frame')['out'][:200])


if __name__ == '__main__':
    main()
