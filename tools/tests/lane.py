"""The test lane this process runs in (docs/10-dev-pipeline.md). SPORE_LANE=N (default 1):
lane 1 = the real install in the "Games" bottle, lane N>1 = bottle SporeLabN + game folder
SPORE-lab/laneN (tools/spore.sh lane create N). Each lane has its own instances, ports and
profiles, so lanes run in parallel without seeing each other."""
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SPORE = os.path.join(ROOT, 'tools', 'spore.sh')

LANE = int(os.environ.get('SPORE_LANE', '1'))
assert 1 <= LANE <= 5, 'lanes 1..5 (console ports 47800+, co-op ports 47810+)'
HOST = 2 * LANE - 1   # SporeKit instance numbers: console port 47800 + N - 1
GUEST = 2 * LANE
PORT = 47810 + HOST - 1  # the host's co-op port
BOTTLE = 'Games' if LANE == 1 else f'SporeLab{LANE}'
APPDATA = os.path.expanduser(f'~/.var/app/com.usebottles.bottles/data/bottles/bottles/{BOTTLE}/drive_c/users/'
                             + os.environ['USER'] + '/AppData/Roaming')
HOST_PROFILE = 'test'
GUEST_PROFILE = 'inst2' if LANE == 1 else f'inst{GUEST}'
HOST_DIR = os.path.join(APPDATA, 'SporeKit-' + HOST_PROFILE, 'Spore')
GUEST_DIR = os.path.join(APPDATA, 'SporeKit-' + GUEST_PROFILE, 'Spore')
JOIN = f'cheat coop join 127.0.0.1:{PORT} Bob'
HOST_CMD = f'cheat coop host {PORT} Alice'


def sh(*args, check=True):
    subprocess.run([SPORE, *args], check=check, stdout=subprocess.DEVNULL)


def run_host():
    sh('run', '-i', str(HOST), '-p', HOST_PROFILE)


def run_guest():
    if LANE == 1:
        sh('run', '-i', '2')
    else:
        sh('run', '-i', str(GUEST), '-p', GUEST_PROFILE)


def out_dir(*sub):
    """per-lane output (logs, screenshots) so parallel lanes do not overwrite each other"""
    p = os.path.join(ROOT, 'out', *(sub if LANE == 1 else (f'lane{LANE}',) + sub))
    os.makedirs(p, exist_ok=True)
    return p
