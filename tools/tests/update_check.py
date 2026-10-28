#!/usr/bin/env python3
"""The installer and the in-game update, end to end, on a clean copy of the game (lane 6 by default:
one game, its own bottle; only TCP console port 47810 + ... is used, not the co-op UDP ports; in the
lab a lane that is neither in the run nor skipped):

  1. the lane gets the game's original files back (no SporeCoop)
  2. SporeCoopSetup.exe /S installs the current build (pack from tools/package.sh)
  3. a local server answers like the GitHub API: release 0.5.1 (a build of this tree that calls
     itself 0.5.1, its pack and SHA256SUMS.txt)
  4. the game finds the update, coop.update apply installs it, after a restart coop 0.5.1 runs
     and the replaced files (*.sporecoop-old) are gone
  5. SporeCoopSetup.exe /S /uninstall puts the game's own steam_api.dll back

  python3 tools/tests/update_check.py [--lane 6]
"""
import argparse
import hashlib
import http.server
import json
import os
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sporectl import Console  # noqa: E402
import lane as lane_mod  # noqa: E402 (game_dir: the lane's game folder from any checkout)

NEW = '0.5.1'
PORT = 47999


def run(*a, **kw):
    return subprocess.run(a, check=True, capture_output=True, text=True, **kw).stdout


class Checks:
    ok = True

    def __call__(self, cond, msg):
        print(('  ok   ' if cond else '  FAIL ') + msg, flush=True)
        Checks.ok &= bool(cond)
        return cond


def build_new(dist):
    """coop.dll that calls itself 0.5.1 + the current stage -> SporeCoop-0.5.1.pack"""
    llvm = run('sed', '-n', 's/^LLVM_MINGW ?= //p', os.path.join(ROOT, 'Makefile')).strip().replace('$(HOME)',
                                                                                                    os.environ['HOME'])
    cxx = f'{llvm}/bin/i686-w64-mingw32-clang++ -DCOOP_VERSION=\\"{NEW}\\"'
    subprocess.run(['make', '-s', '-j16', 'B=build-v' + NEW.replace('.', ''), 'coop', f'CXX={cxx}'], cwd=ROOT,
                   check=True)
    stage = os.path.join(dist, 'stage-new')
    shutil.rmtree(stage, ignore_errors=True)
    shutil.copytree(os.path.join(dist, 'stage'), stage)
    shutil.copy(os.path.join(ROOT, 'build-v' + NEW.replace('.', ''), 'coop.dll'),
                os.path.join(stage, 'SporeCoop', 'mods', 'coop.dll'))
    pack = os.path.join(dist, f'SporeCoop-{NEW}.pack')
    run('python3', os.path.join(ROOT, 'tools', 'mkpack.py'), stage, NEW, pack)
    return pack


def serve(pack):
    """a local stand-in for the GitHub API and its downloads"""
    files = {f'/download/{os.path.basename(pack)}': open(pack, 'rb').read()}
    files['/download/SHA256SUMS.txt'] = (hashlib.sha256(files[f'/download/{os.path.basename(pack)}']).hexdigest()
                                         + f'  {os.path.basename(pack)}\n').encode()
    base = f'http://127.0.0.1:{PORT}'
    files['/latest'] = json.dumps({
        'tag_name': 'v' + NEW, 'html_url': base + '/release',
        'assets': [{'name': n[10:], 'browser_download_url': base + n} for n in files if n.startswith('/download/')],
    }).encode()

    class H(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            body = files.get(self.path)
            self.send_response(200 if body is not None else 404)
            self.send_header('Content-Length', str(len(body or b'')))
            self.end_headers()
            self.wfile.write(body or b'')

        def log_message(self, *a):
            pass

    srv = http.server.ThreadingHTTPServer(('127.0.0.1', PORT), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return base + '/latest'


def wine(bottle, exe, *args):
    """run a Windows program in the lane's bottle and wait for it"""
    w = 'Z:' + exe.replace('/', '\\')
    subprocess.run(['flatpak', 'run', '--command=bottles-cli', 'com.usebottles.bottles', 'run', '-b', bottle, '-e',
                    w, '--', *args], capture_output=True, timeout=300, stdin=subprocess.DEVNULL)
    # Wine keeps its services running after the program exits: stop them, or the next start in
    # this bottle can hang
    n = bottle[len('SporeLab'):]
    assert n.isdigit(), bottle  # never the default lane 1: that kills other tests' games
    subprocess.run([os.path.join(ROOT, 'tools', 'spore.sh'), 'kill'], env=dict(os.environ, SPORE_LANE=n),
                   capture_output=True)


def main():
    ap = argparse.ArgumentParser()
    # the lab gives a lane outside its run (SPORE_FREE_LANE, '' = none free); by hand: lane 6
    free = os.environ.get('SPORE_FREE_LANE')
    ap.add_argument('--lane', type=int, default=int(free) if free else 6)
    a = ap.parse_args()
    if free == '' and '--lane' not in sys.argv:
        print('RESULT: SKIP (no lane outside this lab run is free: this test wipes the mod from its lane)')
        return 0
    assert a.lane > 1, 'never the real install (lane 1)'
    lane, ck = a.lane, Checks()
    env = dict(os.environ, SPORE_LANE=str(lane))
    spore = lambda *x: subprocess.run([os.path.join(ROOT, 'tools', 'spore.sh'), *x], env=env,  # noqa: E731
                                      capture_output=True, text=True)
    print(spore('lane', 'create', str(lane)).stdout.strip())
    spore('kill')
    game = lane_mod.game_dir(lane)  # right from a worktree too (BUG-0068)
    bottle = f'SporeLab{lane}'
    b = os.path.join(game, 'SporebinEP1')
    # 1. the game's own files back
    if os.path.exists(os.path.join(b, 'steam_api_orig.dll')):
        shutil.move(os.path.join(b, 'steam_api_orig.dll'), os.path.join(b, 'steam_api.dll'))
    for p in ('sporekit.dll', 'mods'):
        shutil.rmtree(os.path.join(b, p), ignore_errors=True) if p == 'mods' else \
            (os.path.exists(os.path.join(b, p)) and os.remove(os.path.join(b, p)))
    for p in (os.path.join(game, 'Data', 'SporeCoop.package'),):
        if os.path.exists(p):
            os.remove(p)
    ck(b'sporekit-loader' not in open(os.path.join(b, 'steam_api.dll'), 'rb').read(), 'a clean game (its own steam_api.dll)')
    dist = os.path.join(ROOT, 'out', 'dist')
    ver = run('sed', '-n', 's/^#define COOP_VERSION "\\(.*\\)"/\\1/p', os.path.join(ROOT, 'mods/coop/coop.cpp')).strip()
    cur_pack = os.path.join(dist, f'SporeCoop-{ver}.pack')
    setup = os.path.join(dist, 'SporeCoopSetup.exe')
    new_pack = build_new(dist)
    api = serve(new_pack)
    log = os.path.join(game, 'setup-test.log')  # inside the bottle's view (Flatpak has its own /tmp)
    if os.path.exists(log):
        os.remove(log)
    # 2. the setup installs the current build (offline: its pack)
    wl = lambda p: 'Z:' + p.replace('/', '\\')  # noqa: E731
    wine(bottle, setup, '/S', f'/dir:{wl(game)}', f'/pack:{wl(cur_pack)}', f'/log:{wl(log)}')
    print('  setup:', open(log, errors='replace').read().strip() if os.path.exists(log) else '(no log)')
    ck(b'sporekit-loader' in open(os.path.join(b, 'steam_api.dll'), 'rb').read(), 'the setup installed the loader')
    ck(os.path.exists(os.path.join(b, 'steam_api_orig.dll')), 'and kept the game\'s steam_api.dll')
    ck(open(os.path.join(b, 'mods', 'sporecoop.version')).read().strip() == ver, f'installed version {ver}')
    # 3. the game asks our "GitHub"
    ini = os.path.join(b, 'mods', 'coop.ini')
    with open(ini, 'a') as f:
        f.write(f'\n[update]\ncheck=1\nurl={api}\n')
    inst = 2 * lane - 1

    def start_game():
        spore('run', '-i', str(inst), '-p', 'test')
        for _ in range(120):
            try:
                if Console(inst, timeout=5).call('ping')['ok']:
                    break
            except OSError:
                time.sleep(1)
        c = Console(inst)
        for _ in range(120):
            if 'GalaxyGameEntry' in c.call('game.mode')['out']:
                break
            time.sleep(1)
        return c

    c = start_game()
    st = ''
    for _ in range(40):
        st = c.call('coop.update')['out']
        if 'available' in st or 'up to date' in st:
            break
        time.sleep(1)
    print(' ', st.strip())
    ck('available' in st and 'latest=' + NEW in st, f'the game finds SporeCoop {NEW}')
    ui = ''
    for _ in range(30):  # the menu item and the window are built once the galaxy menu is up
        c.call('coop.uistate 1')
        time.sleep(1)
        ui = c.call('coop.uistate')['out']
        if any(l.startswith('update ') for l in ui.splitlines()):
            break
    import re
    print('  window:', [l for l in ui.splitlines() if l.startswith(('update', 'open'))])
    ck(re.search(r"^update .* 1 'Update'$", ui, re.M), 'the co-op window shows an Update button')
    c.call('shot ' + os.path.join(ROOT, 'out', 'shots', 'update_window.png'))
    time.sleep(1)
    print(' ', c.call('coop.update apply')['out'].strip())
    for _ in range(60):
        st = c.call('coop.update')['out']
        if 'ready' in st or 'failed' in st:
            break
        time.sleep(1)
    print(' ', st.strip())
    ck('ready' in st, 'the update is downloaded, checked and installed')
    ck(open(os.path.join(b, 'mods', 'sporecoop.version')).read().strip() == NEW, f'the version file says {NEW}')
    spore('kill')
    time.sleep(3)
    # 4. after a restart the new mod runs, the replaced files are cleaned up
    c = start_game()
    time.sleep(3)
    mods = c.call('mods')['out']
    print(' ', mods.strip()[:120])
    ck(NEW in mods, f'after the restart coop {NEW} runs')
    old = [os.path.join(dp, f) for dp, _, fs in os.walk(game) for f in fs if f.endswith(('.old', '.sporecoop-old'))
           and ('SporebinEP1' in dp or 'Data' in dp)]
    ck(not [o for o in old if not o.endswith(('sporekit.dll.old', 'sporekit.dll.sporecoop-old'))],
       f'the replaced files are cleaned up ({old})')
    st = c.call('coop.update')['out']
    ck('up to date' in st, 'no further update offered')
    spore('kill')
    time.sleep(3)
    # 5. the setup removes it again
    if os.path.exists(log):
        os.remove(log)
    wine(bottle, setup, '/S', '/uninstall', f'/dir:{wl(game)}', f'/log:{wl(log)}')
    print('  setup:', open(log, errors='replace').read().strip() if os.path.exists(log) else '(no log)')
    ck(b'sporekit-loader' not in open(os.path.join(b, 'steam_api.dll'), 'rb').read(), 'uninstall: the game\'s own steam_api.dll is back')
    ck(not os.path.exists(os.path.join(b, 'sporekit.dll')), 'uninstall: SporeKit is gone')
    print('RESULT:', 'OK' if Checks.ok else 'FAIL')
    return 0 if Checks.ok else 1


if __name__ == '__main__':
    sys.exit(main())
