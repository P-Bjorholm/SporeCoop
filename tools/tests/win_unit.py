#!/usr/bin/env python3
"""Windows-side unit tests (tests/win_tests.cpp): the updater's file handling (BUG-0089, BUG-0114,
BUG-0150) and how a received world is put into a save slot (BUG-0035, BUG-0087, BUG-0105), against
the real Win32 file API under Wine in the lane's bottle. No game is started; the tests work in
%TEMP%\\sporecoop-win-tests of the bottle.

  SPORE_LANE=3 python3 tools/tests/win_unit.py [--old]

--old switches the fixes off: the run must then FAIL (a test you never saw fail proves nothing).
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import lane  # noqa: E402


def main():
    assert lane.LANE > 1, 'never the real install (lane 1)'
    subprocess.run(['make', '-C', ROOT, '--no-print-directory', 'test-win'], check=True, stdout=subprocess.DEVNULL)
    exe = os.path.join(ROOT, 'build', 'win_tests.exe')
    out = os.path.join(lane.out_dir('win_unit'), 'win_tests.txt')
    args = [a for a in sys.argv[1:] if a == '--old']
    w = 'Z:' + exe.replace('/', '\\')
    wo = 'Z:' + out.replace('/', '\\')
    # bottles-cli does not hand the program's stdout back: cmd redirects it into a file
    cmd = f'cmd /c "{w} {" ".join(args)} > {wo} 2>&1"'
    if os.path.exists(out):
        os.remove(out)
    subprocess.run(['flatpak', 'run', '--command=bottles-cli', 'com.usebottles.bottles', 'run', '-b', lane.BOTTLE,
                    '-e', 'C:\\windows\\system32\\cmd.exe', '--', '/c', f'{w} {" ".join(args)} > {wo} 2>&1'],
                   capture_output=True, timeout=300, stdin=subprocess.DEVNULL)
    subprocess.run([os.path.join(ROOT, 'tools', 'spore.sh'), 'kill'], capture_output=True,  # wine services
                   env=dict(os.environ, SPORE_LANE=str(lane.LANE)))
    text = open(out, errors='replace').read() if os.path.exists(out) else ''
    print(text.rstrip())
    ok = 'ALL OK' in text
    print('RESULT:', 'PASS' if ok else 'FAIL', f'({cmd})' if not text else '')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
