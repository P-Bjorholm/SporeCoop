#!/usr/bin/env python3
"""Phase test: the host decides the phase. Host plays the tribe fixture, the guest plays its own
creature-stage save. On join the guest gets the host world in-game and switches to the tribe
stage. Then the guest goes back to its creature save: after a few seconds the host sends the
world again and the guest follows the host's phase once more.

usage: coop_phase.py
"""
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sporectl import Console  # noqa: E402

APPDATA = os.path.expanduser('~/.var/app/com.usebottles.bottles/data/bottles/bottles/Games/drive_c/users/'
                             + os.environ['USER'] + '/AppData/Roaming')
SPORE = os.path.join(ROOT, 'tools', 'spore.sh')


def up(i):
    try:
        return Console(i, timeout=5).call('ping')['ok']
    except OSError:
        return False


def mode_of(c):
    r = c.call('coop.avatar')
    return r['out'].split(':')[0] if r['ok'] else None


def wait_mode(c, name, t=300):
    t0 = time.time()
    while time.time() - t0 < t:
        if mode_of(c) == name:
            return time.time() - t0
        time.sleep(2)
    return None


def main():
    subprocess.run([SPORE, 'kill'], stdout=subprocess.DEVNULL)
    time.sleep(2)
    subprocess.run([SPORE, 'deploy'], check=True, stdout=subprocess.DEVNULL)
    for prof, fx in (('SporeKit-test', 'tribe'), ('SporeKit-inst2', 'creature')):
        for d in ('Games', 'Games-sporecoop-backup'):
            shutil.rmtree(os.path.join(APPDATA, prof, 'Spore', d), ignore_errors=True)
        subprocess.run([SPORE, 'fixture', 'restore', fx, 'test' if prof == 'SporeKit-test' else 'inst2'],
                       check=True, stdout=subprocess.DEVNULL)
    subprocess.run([SPORE, 'run', '-i', '2'], check=True, stdout=subprocess.DEVNULL)
    while not up(2):
        time.sleep(1)
    time.sleep(5)
    subprocess.run([SPORE, 'run', '-p', 'test'], check=True, stdout=subprocess.DEVNULL)
    while not up(1):
        time.sleep(1)
    host, guest = Console(1), Console(2)
    for c, slot, planet, mode in ((guest, 'Game0', 'Scalinus', 'Creature'), (host, 'Game0', 'Cohrach', 'Tribe')):
        for _ in range(180):
            if 'GalaxyGameEntry' in c.call('game.mode')['out']:
                break
            time.sleep(1)
        c.call('wait.frames 900 90000')
        print(c.call(f'coop.load {slot} {planet}')['out'])
    ok = wait_mode(guest, 'Creature') is not None and wait_mode(host, 'Tribe') is not None
    print('guest plays', mode_of(guest), '| host plays', mode_of(host))
    host.call('wait.frames 300 60000')
    print(host.call('cheat coop host 47810 Alice')['out'])
    print(guest.call('cheat coop join 127.0.0.1:47810 Bob')['out'])
    time.sleep(2)
    host.call('click 980 606')  # "game saved" box
    t = wait_mode(guest, 'Tribe')
    print('1) guest followed the host into the tribe stage in-game:', f'{t:.0f}s' if t else 'NO')
    ok &= t is not None
    guest.call('wait.frames 300 60000')
    # the guest leaves for its own creature save: the host must pull it back
    print(guest.call('coop.load Game0 Scalinus')['out'])
    t1 = wait_mode(guest, 'Creature', 120)
    time.sleep(1)
    host.call('click 980 606')
    t2 = wait_mode(guest, 'Tribe', 300)
    print('2) guest went back to Creature:', 'yes' if t1 is not None else 'NO',
          '| host pulled it into its phase again:', f'{t2:.0f}s' if t2 else 'NO')
    ok &= t1 is not None and t2 is not None
    for name, c in (('host', host), ('guest', guest)):
        print(f'{name} faults:', c.call('exc')['out'].splitlines()[0])
    print('RESULT:', 'OK' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
