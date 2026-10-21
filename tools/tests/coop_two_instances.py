#!/usr/bin/env python3
"""Integration test: two game instances on this machine, host + guest.

1. kill old games, deploy build, restore the 'creature' fixture into both test profiles
2. start instance 2 (profile inst2) and instance 1 (profile test), load the saved game
3. instance 1 hosts, instance 2 joins (through the in-game cheat console bridge)
4. move the guest, check both sides see a puppet tracking the remote player
5. screenshots -> out/shots/two_host.png, two_guest.png

usage: coop_two_instances.py [--keep] [--no-restart]
"""
import os
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sporectl import Console, recompress_png  # noqa: E402


def sh(*args):
    subprocess.run([os.path.join(ROOT, 'tools', 'spore.sh'), *args], check=True,
                   stdout=subprocess.DEVNULL)


def load(inst, results):
    r = subprocess.run([sys.executable, os.path.join(HERE, 'load_game.py'), '-i', str(inst)],
                       capture_output=True, text=True, timeout=600)
    results[inst] = r.stdout + r.stderr


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


def shot(con, name):
    path = os.path.join(ROOT, 'out', 'shots', name)
    r = con.call('shot ' + path)
    if r['ok']:
        recompress_png(path, 0.5)
    return path


def main():
    restart = '--no-restart' not in sys.argv
    if restart:
        sh('kill')
        time.sleep(2)
        sh('deploy')
        sh('fixture', 'restore', 'creature', 'test')
        sh('fixture', 'restore', 'creature', 'inst2')
        sh('run', '-i', '2')
        assert wait_up(2), 'instance 2 did not start'
        time.sleep(5)
        sh('run', '-p', 'test')
        assert wait_up(1), 'instance 1 did not start'
        res = {}
        th = [threading.Thread(target=load, args=(i, res)) for i in (1, 2)]
        for t in th:
            t.start()
        for t in th:
            t.join()
        for i in (1, 2):
            print(f'instance {i}:', res.get(i, '').splitlines()[:1])
    host, guest = Console(1), Console(2)
    print(host.call('cheat coop host 47810 Alice')['out'])
    print(guest.call('cheat coop join 127.0.0.1:47810 Bob')['out'])
    time.sleep(3)
    # walk the guest somewhere: click on the ground left of the screen
    guest.call('click 300 700')
    ok = True
    for _ in range(6):
        time.sleep(1)
        for name, con in (('host', host), ('guest', guest)):
            st = con.call('coop.status')['out']
            line = [l for l in st.splitlines() if 'puppet pos' in l]
            print(f'{name}: {line[0].strip() if line else "NO PUPPET"}')
            ok &= bool(line)
    # M2: NPC proxies + host-authoritative damage
    time.sleep(6)
    glist = guest.call('coop.npc list')['out'].splitlines()
    spawned = [l for l in glist if l.strip().startswith('#') and 'spawned' in l and 'DEAD' not in l]
    print('guest:', glist[0] if glist else '?')
    ok &= len(spawned) > 0
    if spawned:
        nid = spawned[0].split()[0].lstrip('#')

        def host_hp():
            for l in host.call('coop.npc list')['out'].splitlines():
                if l.strip().startswith('#' + nid + ' '):
                    return float(l.split('hp=')[1].split()[0])
            return None
        def applied():
            return int(host.call('coop.npc')['out'].split('hits_applied=')[1].split()[0])
        hp0, a0 = host_hp(), applied()
        guest.call(f'coop.npc hit {nid} 1')
        time.sleep(0.5)
        hp1, a1 = host_hp(), applied()
        print(f'npc #{nid}: host hp {hp0} -> {hp1}, hits applied {a0} -> {a1}')
        # the NPC may flee out of range right after the hit; the host must have applied it
        ok &= a1 == a0 + 1 and (hp1 is None or hp1 < hp0)
    # M3: shared DNA
    d0 = float(host.call('coop.dna')['out'].split('=')[1])
    guest.call('coop.dna add 5')
    time.sleep(1.5)
    d1 = float(host.call('coop.dna')['out'].split('=')[1])
    d2 = float(guest.call('coop.dna')['out'].split('=')[1])
    print(f'dna host {d0} -> {d1}, guest {d2}')
    ok &= abs(d1 - d0 - 5) < 0.01 and abs(d2 - d1) < 0.01
    print('screens:', shot(host, 'two_host.png'), shot(guest, 'two_guest.png'))
    print('host faults:', host.call('exc')['out'].splitlines()[0])
    print('guest faults:', guest.call('exc')['out'].splitlines()[0])
    print('RESULT:', 'OK' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
