#!/usr/bin/env python3
"""Knowledge check (docs/10-dev-pipeline.md): every game symbol the code asks for by name must be
in data/symbols/*.sym of EVERY supported build (<name>_<exe hash>.sym; a missing name returns 0 and
silently turns a feature off on that build), and every entry of the hand-written overrides files
must say how sure it is (verified / likely / unknown)."""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
known = {}
by_build = {}  # exe hash (file name suffix) -> names
for f in glob.glob(os.path.join(ROOT, 'data', 'symbols', '*.sym')):
    for i, line in enumerate(open(f, errors='replace'), 1):
        m = re.match(r'\s*([^=;#\s]+)\s*=\s*(0x[0-9A-Fa-f]+)\s*(;.*)?$', line)
        if m:
            by_build.setdefault(os.path.basename(f)[:-4].rsplit('_', 1)[-1], set()).add(m.group(1))
            known.setdefault(m.group(1), (os.path.basename(f), i, (m.group(3) or '').strip()))
used = {}
for f in glob.glob(os.path.join(ROOT, 'mods', '**', '*.cpp'), recursive=True) + \
        glob.glob(os.path.join(ROOT, 'sporekit', '*.cpp')):
    for i, line in enumerate(open(f, errors='replace'), 1):
        for m in re.finditer(r'\b(?:sym|S|sym_addr)\(\s*"([A-Za-z_][\w:]*)"\s*\)', line):
            used.setdefault(m.group(1), f'{os.path.relpath(f, ROOT)}:{i}')
bad = 0
for name, where in sorted(used.items()):
    if name not in known:
        print(f'missing symbol {name} (used at {where}): add it to data/symbols/overrides_*.sym')
        bad += 1
    else:
        for build, names in sorted(by_build.items()):
            if name not in names:
                print(f'missing symbol {name} (used at {where}) for build {build}: port it to '
                      f'data/symbols/overrides_{build}.sym (docs/05-re-notes.md 5.1.1)')
                bad += 1
for name, (f, i, note) in sorted(known.items()):
    if f.startswith('overrides') and not re.search(r'\b(verified|likely|unknown|high|ported)\b', note, re.I):
        print(f'{f}:{i} {name}: say how sure it is (verified / likely / unknown) and what it does')
        bad += 1
print(f're_lint: {len(used)} symbols used by the code, {len(known)} known, builds {", ".join(sorted(by_build))}, {bad} problem(s)')
sys.exit(1 if bad else 0)
