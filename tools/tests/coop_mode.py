#!/usr/bin/env python3
"""Integration test for any game mode: instance 1 (host) plays a saved game, instance 2
(guest) starts in the galaxy menu with an empty profile, joins, receives and auto-loads the
host world, then both must see each other's puppet.

usage: coop_mode.py [--fixture NAME --planet SLOT/PLANET] [--keep-guest] [--move X,Y]
  --fixture      restart both games, restore out/fixtures/NAME.tgz for the host and load
                 SLOT/PLANET there (e.g. --fixture space --planet Game0/Maraya). Without it the
                 host must already be playing. (Quitting one Wine instance of the bottle kills
                 the others, so the guest is only restarted together with the host.)
  --keep-guest   reuse a running guest
  --move X,Y     host clicks there to move (default 1000,760)
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from twoinst import wait_guest_in  # noqa: E402
from sporectl import Console, recompress_png  # noqa: E402

APPDATA = os.path.expanduser('~/.var/app/com.usebottles.bottles/data/bottles/bottles/Games/drive_c/users/'
                             + os.environ['USER'] + '/AppData/Roaming')


def up(inst):
    try:
        return Console(inst, timeout=5).call('ping')['ok']
    except OSError:
        return False


def out(con, line):
    return con.call(line).get('out', '')


def shot(con, name):
    path = os.path.join(ROOT, 'out', 'shots', name)
    if con.call('shot ' + path)['ok']:
        recompress_png(path, 0.5)
    return path


def puppet_line(con):
    for l in out(con, 'coop.status').splitlines():
        if 'puppet pos' in l:
            return l.strip()
    return None


def avatar_pos(con):
    for l in out(con, 'coop.avatar').splitlines():
        if l.startswith('pos='):
            return [float(v) for v in l[5:].split(')')[0].split()]
    return None


def remote_pos(con):
    for l in out(con, 'coop.status').splitlines():
        if l.startswith('remote ') and 'pos=(' in l:
            return [float(v) for v in l.split('pos=(')[1].split(')')[0].split()]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--keep-guest', action='store_true')
    ap.add_argument('--fixture')
    ap.add_argument('--adventure', action='store_true', help='restart both and start the tutorial adventure on both')
    ap.add_argument('--planet')
    ap.add_argument('--port', type=int, default=47810)
    ap.add_argument('--move', default='1000,760')
    a = ap.parse_args()
    spore = os.path.join(ROOT, 'tools', 'spore.sh')
    if a.adventure:
        subprocess.run([spore, 'kill'], stdout=subprocess.DEVNULL)
        time.sleep(2)
        subprocess.run([spore, 'deploy'], check=True, stdout=subprocess.DEVNULL)
        # the Galactic Adventures "play" button offers the next adventure of the profile's
        # campaign: give the guest the host's progress so both get the same adventure
        src, dst = (os.path.join(APPDATA, p, 'Spore') for p in ('SporeKit-test', 'SporeKit-inst2'))
        for f in os.listdir(src):
            if f.endswith('.package'):
                shutil.copy2(os.path.join(src, f), os.path.join(dst, f))
        subprocess.run([spore, 'run', '-i', '2'], check=True, stdout=subprocess.DEVNULL)
        for _ in range(120):
            if up(2):
                break
            time.sleep(1)
        time.sleep(5)
        subprocess.run([spore, 'run', '-p', 'test'], check=True, stdout=subprocess.DEVNULL)
        for _ in range(120):
            if up(1):
                break
            time.sleep(1)
        procs = [subprocess.Popen([sys.executable, os.path.join(HERE, 'adventure_start.py'), '-i', str(i)])
                 for i in (1, 2)]
        for p in procs:
            p.wait()
        a.keep_guest = True
    if a.fixture:
        subprocess.run([spore, 'kill'], stdout=subprocess.DEVNULL)
        time.sleep(2)
        subprocess.run([spore, 'deploy'], check=True, stdout=subprocess.DEVNULL)
        shutil.rmtree(os.path.join(APPDATA, 'SporeKit-test', 'Spore', 'Games'), ignore_errors=True)
        subprocess.run([spore, 'fixture', 'restore', a.fixture, 'test'], check=True, stdout=subprocess.DEVNULL)
        for d in ('Games', 'Games-sporecoop-backup'):
            shutil.rmtree(os.path.join(APPDATA, 'SporeKit-inst2', 'Spore', d), ignore_errors=True)
        subprocess.run([spore, 'run', '-i', '2'], check=True, stdout=subprocess.DEVNULL)
        for _ in range(120):
            if up(2):
                break
            time.sleep(1)
        time.sleep(5)
        subprocess.run([spore, 'run', '-p', 'test'], check=True, stdout=subprocess.DEVNULL)
        for _ in range(120):
            if up(1):
                break
            time.sleep(1)
        h = Console(1)
        for _ in range(180):
            if 'GalaxyGameEntry' in out(h, 'game.mode'):
                break
            time.sleep(1)
        h.call('wait.frames 900 90000')
        slot, planet = a.planet.split('/')
        print(out(h, f'coop.load {slot} {planet}'))
        t0 = time.time()
        while time.time() - t0 < 240 and not h.call('coop.avatar')['ok']:
            time.sleep(2)
        h.call('wait.frames 600 90000')
        a.keep_guest = True  # fresh guest already waits in the menu
    host = Console(1)
    hav = host.call('coop.avatar')
    assert hav['ok'], 'host is not playing: ' + hav.get('out', '')
    mode = hav['out'].split(':')[0]
    print('host plays', mode)
    out(host, 'coop.leave')
    print(out(host, f'cheat coop host {a.port} Alice'))
    if not a.keep_guest:
        if up(2):
            Console(2).call('quit')
            for _ in range(60):
                if not up(2):
                    break
                time.sleep(1)
            time.sleep(3)
        for d in ('Games', 'Games-sporecoop-backup'):
            shutil.rmtree(os.path.join(APPDATA, 'SporeKit-inst2', 'Spore', d), ignore_errors=True)
        subprocess.run([os.path.join(ROOT, 'tools', 'spore.sh'), 'run', '-i', '2'], check=True,
                       stdout=subprocess.DEVNULL)
        for _ in range(120):
            if up(2):
                break
            time.sleep(1)
    guest = Console(2)
    for _ in range(180):
        m = out(guest, 'game.mode')
        if 'GalaxyGameEntry' in m or guest.call('coop.avatar')['ok']:
            break
        time.sleep(1)
    guest.call('wait.frames 700 60000')
    print(out(guest, f'cheat coop join 127.0.0.1:{a.port} Bob'))
    t0 = time.time()
    # host saved before sharing: close the confirmation box (Enter) whenever it stops the host's
    # frames; a click could land in the game while the host is still saving
    wait_guest_in(host, guest, 300)
    gav = guest.call('coop.avatar')
    print(f'guest after {time.time() - t0:.0f}s:', gav['out'].splitlines()[0] if gav['out'] else '?')
    ok = gav['ok'] and gav['out'].split(':')[0] == mode
    guest.call('wait.frames 400 60000')
    for name, con in (('host', host), ('guest', guest)):
        l = puppet_line(con)
        print(f'{name}: {l or "NO PUPPET"}')
        ok &= l is not None
    # host moves; the guest must follow the host's position
    mx, my = map(int, a.move.split(','))
    p0 = avatar_pos(host)
    host.call(f'click {mx} {my}')
    time.sleep(4)
    p1 = avatar_pos(host)
    r1 = remote_pos(guest)
    if p0 and p1 and r1:
        moved = sum((x - y) ** 2 for x, y in zip(p0, p1)) ** 0.5
        err = sum((x - y) ** 2 for x, y in zip(p1, r1)) ** 0.5
        print(f'host moved {moved:.1f} m, guest sees it {err:.1f} m off')
        ok &= err < 5.0
    else:
        print('positions missing', p0, p1, r1)
        ok = False
    l = puppet_line(guest)
    print('guest puppet:', l)
    print('screens:', shot(host, 'mode_host.png'), shot(guest, 'mode_guest.png'))
    for name, con in (('host', host), ('guest', guest)):
        print(f'{name} faults:', out(con, 'exc').splitlines()[0])
    print('RESULT:', 'OK' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
