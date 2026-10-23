#!/usr/bin/env python3
"""Put the window of a test game into an agent-ws project (its own workspace), so test games never
land on the user's desktop. Bottles drops the environment marker of `agent-ws launch`, so the window
is found by its process: the Wine command line holds the lane's game folder and the run's arguments.

  assign_windows.py <project> <game folder name> "<game arguments>"   (started by tools/spore.sh run)
"""
import json
import subprocess
import sys
import time

project, folder, args = sys.argv[1], sys.argv[2], sys.argv[3]
want_exe = f'\\{folder}\\SporebinEP1\\SporeApp.exe'.lower()
t0 = time.time()
done = set()
while time.time() - t0 < 180:
    try:
        wins = json.loads(subprocess.run(['agent-ws', 'windows', '--json'], capture_output=True, text=True,
                                         timeout=10).stdout or '[]')
    except (OSError, ValueError, subprocess.TimeoutExpired):
        wins = []
    for w in wins:
        if not str(w.get('wmClass', '')).lower().startswith('sporeapp') or w['id'] in done:
            continue
        try:
            cmd = open(f'/proc/{w["pid"]}/cmdline', 'rb').read().replace(b'\0', b' ').decode('utf-8', 'replace')
        except OSError:
            continue
        exe, _, rest = cmd.strip().partition(' ')
        if not exe.lower().endswith(want_exe) or rest.split() != args.split():
            continue  # another lane, another instance, or the player's own game
        if w.get('project') != project:
            subprocess.run(['agent-ws', 'assign', str(w['id']), project], capture_output=True)
        done.add(w['id'])
    if done:
        break
    time.sleep(1)
