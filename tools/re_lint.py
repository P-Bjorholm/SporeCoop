#!/usr/bin/env python3
"""Knowledge check (docs/10-dev-pipeline.md): every game symbol the code asks for by name must be
in data/symbols/*.sym of EVERY supported build (GOG 2016 is frozen after 0.7: only noted) (<name>_<exe hash>.sym; a missing name returns 0 and
silently turns a feature off on that build), and every entry of the hand-written overrides files
must say how sure it is (verified / likely / unknown)."""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
known = {}
by_build = {}  # exe hash (file name suffix) -> names
checked = {}  # exe hash -> names in the hand-written overrides file (a signature checked by a person)
for f in sorted(glob.glob(os.path.join(ROOT, 'data', 'symbols', '*.sym'))):
    build = os.path.basename(f)[:-4].rsplit('_', 1)[-1]
    for i, line in enumerate(open(f, errors='replace'), 1):
        m = re.match(r'\s*([^=;#\s]+)\s*=\s*(0x[0-9A-Fa-f]+)\s*(;.*)?$', line)
        if m:
            by_build.setdefault(build, set()).add(m.group(1))
            if os.path.basename(f).startswith('overrides'):
                checked.setdefault(build, set()).add(m.group(1))
            known.setdefault(m.group(1), (os.path.basename(f), i, (m.group(3) or '').strip()))
used = {}
for f in sorted(glob.glob(os.path.join(ROOT, 'mods', '**', '*.cpp'), recursive=True)) + \
        sorted(glob.glob(os.path.join(ROOT, 'sporekit', '*.cpp'))):
    for i, line in enumerate(open(f, errors='replace'), 1):
        for m in re.finditer(r'\b(?:sym|S|sym_addr)\(\s*"([A-Za-z_][\w:]*)"\s*\)', line):
            used.setdefault(m.group(1), f'{os.path.relpath(f, ROOT)}:{i}')
# builds that are no longer supported (CLAUDE.md: GOG 2016 after 0.7): their symbol files stay
# frozen, code added later may lack their symbols (the feature stays off on such a build)
FROZEN = {'922af7c7'}
frozen_missing = []
bad = 0
for name, where in sorted(used.items()):
    if name not in known:
        print(f'missing symbol {name} (used at {where}): add it to data/symbols/overrides_*.sym')
        bad += 1
    else:
        for build, names in sorted(by_build.items()):
            if name not in names:
                if build in FROZEN:  # no longer supported: a note, not a failure
                    frozen_missing.append(name)
                    continue
                print(f'missing symbol {name} (used at {where}) for build {build}: port it to '
                      f'data/symbols/overrides_{build}.sym (docs/05-re-notes.md 5.1.1)')
                bad += 1
# BUG-0143: a name only in a generated file (ModAPI's address, "high" / "medium") has no checked
# convention, argument count or return: every name the code uses needs an overrides entry of every
# supported build, with its confidence and signature
for name, where in sorted(used.items()):
    for build in sorted(by_build):
        if build in FROZEN or name not in by_build[build] or name in checked.get(build, ()):
            continue
        print(f'unchecked symbol {name} (used at {where}) for build {build}: only in a generated .sym; check its '
              f'signature in the disassembly and add it to data/symbols/overrides_{build}.sym')
        bad += 1
# every line of every hand-written overrides file, also when a generated file has the same name
# (the usual reason to write an override) or the name appears twice (BUG-0086)
for f in sorted(glob.glob(os.path.join(ROOT, 'data', 'symbols', 'overrides_*.sym'))):
    for i, line in enumerate(open(f, errors='replace'), 1):
        m = re.match(r'\s*([^=;#\s]+)\s*=\s*(0x[0-9A-Fa-f]+)\s*(;.*)?$', line)
        if m and not re.search(r'\b(verified|likely|unknown)\b', m.group(3) or '', re.I):
            print(f'{os.path.basename(f)}:{i} {m.group(1)}: say how sure it is (verified / likely / unknown) '
                  f'and what it does')
            bad += 1
if frozen_missing:
    # the features behind these names are off on the frozen build (BUG-0108: say which)
    print(f're_lint: {len(frozen_missing)} symbol(s) missing for the frozen build(s) {", ".join(sorted(FROZEN))} '
          f'(not supported, these features are off there): {", ".join(sorted(set(frozen_missing)))}')
print(f're_lint: {len(used)} symbols used by the code, {len(known)} known, builds {", ".join(sorted(by_build))}, {bad} problem(s)')
sys.exit(1 if bad else 0)
