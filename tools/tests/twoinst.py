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
import lane  # noqa: E402
import fixtures  # noqa: E402
from lane import APPDATA, sh  # noqa: E402,F401


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
        # in the host's world: the guest plays and the host sees it in the same place
        if guest.call('coop.avatar')['ok'] and 'together 1 1' in host.call('coop.status')['out']:
            return True
    return False


def shot(con, name):
    path = os.path.join(lane.out_dir('shots'), name)
    if con.call('shot ' + path)['ok']:
        recompress_png(path, 0.5)
    return path


class Monitor(threading.Thread):
    """Invariants during a test (docs/10-dev-pipeline.md): a game whose frames stop for more than
    FREEZE seconds is frozen (the SporeKit watchdog logs the main thread stack at 30 s); a game whose
    console stops answering has crashed. Uses only any-thread console commands (frame, stack),
    so it works while the main thread hangs. Checks.faults() fails the test on any event."""
    FREEZE = 10.0
    events = []

    def __init__(self, inst, name):
        super().__init__(daemon=True)
        self.inst, self.name, self.stop = inst, name, False

    def run(self):
        import re
        con = Console(self.inst, timeout=10)
        last, since, reported = -1, time.time(), False
        while not self.stop:
            try:
                m = re.search(r'frame=(\d+)', con.call('frame').get('out', ''))
            except (OSError, ValueError):
                if not self.stop:
                    Monitor.events.append(f'{self.name}: the console stopped answering (crash?)')
                return
            f = int(m.group(1)) if m else -1
            now = time.time()
            if f != last:
                if reported:
                    Monitor.events.append(f'{self.name}: frames resumed after {now - since:.0f} s')
                last, since, reported = f, now, False
            elif not reported and now - since > self.FREEZE:
                reported = True
                # no stack from here: suspending the main thread under Wine can hang the console too;
                # the SporeKit watchdog writes it into the game log after 30 s (lab.py keeps that log)
                Monitor.events.append(f'{self.name}: FROZE (no frame for {self.FREEZE:.0f} s; main thread stack: '
                                      f'watchdog "no frames" in the {self.name} game log)')
            time.sleep(1)


_monitors = []


def watch(host_inst, guest_inst):
    for m in _monitors:
        m.stop = True
    _monitors.clear()
    Monitor.events.clear()
    for inst, name in ((host_inst, 'host'), (guest_inst, 'guest')):
        m = Monitor(inst, name)
        m.start()
        _monitors.append(m)


def build_id():
    """what is deployed: the build outputs and the symbol files (size + mtime)"""
    import glob
    import hashlib
    h = hashlib.sha1()
    for f in sorted(glob.glob(os.path.join(ROOT, 'build', '*.dll')) + glob.glob(os.path.join(ROOT, 'build', '*.package'))
                    + glob.glob(os.path.join(ROOT, 'data', 'symbols', '*.sym'))):
        st = os.stat(f)
        h.update(f'{os.path.basename(f)} {st.st_size} {st.st_mtime_ns}'.encode())
    return h.hexdigest()[:16]


STATE = os.path.join(lane.out_dir(), 'lane_state.json')


def lane_state(**kw):
    import json
    if kw:
        json.dump(kw, open(STATE, 'w'))
        return kw
    try:
        return json.load(open(STATE))
    except (OSError, ValueError):
        return {}


def alive(inst):
    try:
        return Console(inst, timeout=5).call('ping')['ok']
    except OSError:
        return False


def restore_creations():
    """both players' own creations ("My Creations", EditorSaves.package) are test data too: every
    lane starts with the same ones (fixtures creations-host / creations-guest)"""
    import shutil
    fx = os.path.join(os.path.dirname(os.path.dirname(subprocess.run(
        ['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=ROOT, capture_output=True,
        text=True).stdout.strip())), 'SporeCoop', 'out', 'fixtures')
    for name, d in (('creations-host', lane.HOST_DIR), ('creations-guest', lane.GUEST_DIR)):
        src = os.path.join(fx, name + '.package')
        if os.path.exists(src):
            os.makedirs(d, exist_ok=True)
            shutil.copy(src, os.path.join(d, 'EditorSaves.package'))


def check_fixture(host, fixture):
    bad = [what for what, ok in fixtures.check(host, fixture) if not ok]
    if bad:
        print(f'  (fixture {fixture} lacks: {", ".join(bad)} - tools/tests/fixtures.py)', flush=True)
    return not bad


def warm_reset(host, guest, fixture, planet):
    """the running games of the lane start the test again: the fixture goes into a fresh save
    slot (the loaded one must not change on disk), the host loads it, the guest joins again"""
    t0 = time.time()
    st = lane_state()
    slot = 10 + (st.get('slot', 9) - 9) % 10
    for c in (guest, host):
        c.call('coop.leave')
    sh('fixture', 'slot', fixture, lane.HOST_PROFILE, str(slot))
    host.call(f'coop.load Game{slot} {planet.split("/")[1]}')
    phases = []
    while time.time() - t0 < 120:
        m = host.call('game.mode')['out']
        if 'Load' not in m and 'Menu' not in m and host.call('coop.avatar')['ok']:
            break
        time.sleep(0.3)
    else:
        return False
    phases.append(('host in game', time.time() - t0))
    host.call(lane.HOST_CMD)
    guest.call(lane.JOIN)
    if not wait_guest_in(host, guest, 120):
        return False
    phases.append(('guest in game', time.time() - t0))
    lane_state(**{**st, 'slot': slot, 'fixture': fixture, 'planet': planet, 'warm_runs': st.get('warm_runs', 0) + 1})
    time.sleep(3)
    print(f'  (warm reset in {time.time() - t0:.0f} s: ' + ', '.join(f'{n} {t:.0f}' for n, t in phases)
          + f'; warm run {st.get("warm_runs", 0) + 1} of these games)', flush=True)
    return True


def start(fixture='creature', planet=None, load_args=()):
    """kill, deploy, restore the fixture for the host, empty guest profile, start both,
    load the host game (tools/tests/load_game.py), host + join; returns (host, guest).
    With SPORE_WARM=1 (tools/lab.py) the running games of the lane are reused when they run the
    same build: warm_reset() instead of a restart (~10 s instead of ~40 s)."""
    if planet is None and not load_args:
        planet = fixtures.planet_of(fixture)  # loaded by name (no menu clicks, warm-capable)
    st = lane_state()
    # warm only within the same save: a test starts from its fixture (stage switches in game work,
    # coop_stage_switch.py, but go through the galaxy menu and take longer)
    if os.environ.get('SPORE_WARM') == '1' and planet and st.get('build') == build_id() \
            and st.get('fixture') == fixture and alive(lane.HOST) and alive(lane.GUEST):
        host, guest = Console(lane.HOST), Console(lane.GUEST)
        try:
            if warm_reset(host, guest, fixture, planet):
                watch(lane.HOST, lane.GUEST)
                check_fixture(host, fixture)
                return host, guest
        except (OSError, ValueError) as e:
            print('  (warm reset failed:', e, ')', flush=True)
        print('  (warm reset failed: cold start)', flush=True)
    lane_state()  # (the state is rewritten below once the games run)
    sh('kill')
    time.sleep(2)
    sh('deploy')
    sh('fixture', 'restore', fixture, lane.HOST_PROFILE)
    restore_creations()
    subprocess.run(['rm', '-rf', lane.GUEST_DIR + '/Games', lane.GUEST_DIR + '/Games-sporecoop-backup'])
    for d in (lane.HOST_DIR, lane.GUEST_DIR):  # own-creation choices: back to the defaults
        subprocess.run(['rm', '-f', d + '/SporeCoop/choices.ini'])
    t_start = time.time()
    lane.run_guest()
    time.sleep(2)  # two sandboxes starting at the very same moment can race on the bottle
    lane.run_host()
    for inst, run in ((lane.GUEST, lane.run_guest), (lane.HOST, lane.run_host)):
        if not wait_up(inst, 90):  # two sandboxes starting together sometimes lose one: start it again
            run()
            assert wait_up(inst), f'instance {inst} did not start'
    phases = [('wine up', time.time() - t_start)]
    host, guest = Console(lane.HOST), Console(lane.GUEST)
    if planet:  # GameN/Planet of the fixture, loaded through the mod (works from the galaxy menu)
        for _ in range(180):
            if 'GalaxyGameEntry' in host.call('game.mode')['out']:
                break
            time.sleep(1)
        phases.append(('host menu', time.time() - t_start))
        host.call('wait.frames 240 90000')  # the galaxy menu settles (8 s)
        slot, name = planet.split('/')
        host.call(f'coop.load {slot} {name}')
        t0 = time.time()
        while time.time() - t0 < 240 and not host.call('coop.avatar')['ok']:
            time.sleep(2)
        phases.append(('host in game', time.time() - t_start))
        time.sleep(4)
    else:
        subprocess.run([sys.executable, os.path.join(HERE, 'load_game.py'), '-i', str(lane.HOST), *load_args],
                       capture_output=True, text=True, timeout=600)
    host.call(lane.HOST_CMD)
    for _ in range(120):
        if 'GalaxyGameEntry' in guest.call('game.mode')['out']:
            break
        time.sleep(1)
    phases.append(('guest menu', time.time() - t_start))
    guest.call('wait.frames 200 60000')
    phases.append(('guest join', time.time() - t_start))
    guest.call(lane.JOIN)
    # the host saves before sharing and the game shows a modal "game saved" box (frames stop):
    # close it whenever it is up, until the guest is in the world
    wait_guest_in(host, guest)
    phases.append(('guest in game', time.time() - t_start))
    time.sleep(4)
    print(f'  (both games ready in {time.time() - t_start:.0f} s: '
          + ', '.join(f'{n} {t:.0f}' for n, t in phases) + ')', flush=True)
    lane_state(build=build_id(), fixture=fixture, planet=planet, slot=9, warm_runs=0)
    watch(lane.HOST, lane.GUEST)
    check_fixture(host, fixture)
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
            out = con.call('exc')['out'].splitlines()
            print(f'{name} faults: {out[0]}')
            for line in out[1:4]:
                print(f'    {line.strip()}')
            self.ok &= 'total faults: 0' in out[0]
        for e in Monitor.events:  # freezes / crashes seen while the test ran
            print('  FAIL invariant:', e)
            self.ok = False

    def result(self):
        print('RESULT:', 'OK' if self.ok else 'FAIL')
        return 0 if self.ok else 1
