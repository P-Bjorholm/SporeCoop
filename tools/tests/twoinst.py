"""Shared setup for two-instance tests: host = instance 1 (profile test) with a fixture save,
guest = instance 2 (empty profile) that joins from the menu and receives the host world."""
import os
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sporectl import Console, recompress_png  # noqa: E402

APPDATA = os.path.expanduser('~/.var/app/com.usebottles.bottles/data/bottles/bottles/Games/drive_c/users/'
                             + os.environ['USER'] + '/AppData/Roaming')


def sh(*args):
    subprocess.run([os.path.join(ROOT, 'tools', 'spore.sh'), *args], check=True, stdout=subprocess.DEVNULL)


def wait_up(inst, t=120):
    t0 = time.time()
    while time.time() - t0 < t:
        try:
            if Console(inst).call('ping')['ok']:
                return True
        except OSError:
            pass
        time.sleep(1)
    return False


def host_frame(con):
    import re
    m = re.search(r'frame=(\d+)', con.call('frame').get('out', ''))
    return int(m.group(1)) if m else -1


def wait_guest_in(host, guest, timeout=240, close=lambda h: h.call('key enter')):
    """The host saves before sharing its world and the game shows a modal "game saved" box (the
    host's frames stop): close it whenever it is up, until the guest is in the world."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        f1 = host_frame(host)
        time.sleep(0.7)
        if host_frame(host) == f1:
            close(host)
        if guest.call('coop.avatar')['ok']:
            return True
    return False


def shot(con, name):
    path = os.path.join(ROOT, 'out', 'shots', name)
    if con.call('shot ' + path)['ok']:
        recompress_png(path, 0.5)
    return path


def start(fixture='creature', planet=None, load_args=()):
    """kill, deploy, restore the fixture for the host, empty guest profile, start both,
    load the host game (tools/tests/load_game.py), host + join; returns (host, guest)."""
    sh('kill')
    time.sleep(2)
    sh('deploy')
    sh('fixture', 'restore', fixture, 'test')
    subprocess.run(['rm', '-rf', APPDATA + '/SporeKit-inst2/Spore/Games', APPDATA + '/SporeKit-inst2/Spore/Games-sporecoop-backup'])
    t_start = time.time()
    sh('run', '-i', '2')
    time.sleep(2)  # two sandboxes starting at the very same moment can race on the bottle
    sh('run', '-p', 'test')
    for inst, args in ((2, ('-i', '2')), (1, ('-p', 'test'))):
        if not wait_up(inst, 90):  # two sandboxes starting together sometimes lose one: start it again
            sh('run', *args)
            assert wait_up(inst), f'instance {inst} did not start'
    host, guest = Console(1), Console(2)
    if planet:  # GameN/Planet of the fixture, loaded through the mod (works from the galaxy menu)
        for _ in range(180):
            if 'GalaxyGameEntry' in host.call('game.mode')['out']:
                break
            time.sleep(1)
        host.call('wait.frames 240 90000')  # the galaxy menu settles (8 s)
        slot, name = planet.split('/')
        host.call(f'coop.load {slot} {name}')
        t0 = time.time()
        while time.time() - t0 < 240 and not host.call('coop.avatar')['ok']:
            time.sleep(2)
        time.sleep(4)
    else:
        subprocess.run([sys.executable, os.path.join(HERE, 'load_game.py'), '-i', '1', *load_args],
                       capture_output=True, text=True, timeout=600)
    host.call('cheat coop host 47810 Alice')
    for _ in range(120):
        if 'GalaxyGameEntry' in guest.call('game.mode')['out']:
            break
        time.sleep(1)
    guest.call('wait.frames 200 60000')
    guest.call('cheat coop join 127.0.0.1:47810 Bob')
    # the host saves before sharing and the game shows a modal "game saved" box (frames stop):
    # close it whenever it is up, until the guest is in the world
    wait_guest_in(host, guest)
    time.sleep(4)
    print(f'  (both games ready in {time.time() - t_start:.0f} s)', flush=True)
    return host, guest


class Checks:
    def __init__(self):
        self.ok = True

    def __call__(self, cond, msg):
        print(('  ok   ' if cond else '  FAIL ') + msg, flush=True)
        self.ok &= bool(cond)
        return cond

    def faults(self, **cons):
        for name, con in cons.items():
            f = con.call('exc')['out'].splitlines()[0]
            print(f'{name} faults: {f}')
            self.ok &= 'total faults: 0' in f

    def result(self):
        print('RESULT:', 'OK' if self.ok else 'FAIL')
        return 0 if self.ok else 1
