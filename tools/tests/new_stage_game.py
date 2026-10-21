#!/usr/bin/env python3
"""Start a new game at any stage from the galaxy menu via the stage chooser (cheat levels -unlock).

Coordinates are for the 1440x1080 windowed test profile. Usage:
  new_stage_game.py <cell|creature|tribe|civ|space> [-i N] [--slot X,Y]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from sporectl import Console  # noqa: E402
import subprocess  # noqa: E402

STAGE_ICON = {"cell": (720, 392), "creature": (800, 428), "tribe": (848, 496), "civ": (848, 578),
              "space": (798, 646)}
MODE = {"cell": "Cell", "creature": "Creature", "tribe": "Tribe", "civ": "Civ", "space": "Space"}
DICE_CARD, DICE_NAME, DICE_STUFF = (930, 486), (1102, 474), (1092, 495)
NEXT = (1086, 618)

# wizard steps after choosing the stage: ("click", x, y) / ("wait", frames)
STEPS = {
    "space": [DICE_CARD, NEXT, DICE_NAME, NEXT, NEXT, NEXT, NEXT, DICE_STUFF, NEXT, (1090, 608)],
    "civ": [DICE_CARD, NEXT, NEXT, NEXT, NEXT, DICE_STUFF, NEXT, (1090, 608)],  # card, theme, difficulty, specialty, city stuff, planet
    # diet card, sporepedia creature + confirm, theme, difficulty, planet name
    "tribe": [(852, 536), NEXT, (428, 220), (1400, 1058), NEXT, NEXT, (1090, 608)],
    "creature": [DICE_CARD, NEXT, DICE_NAME, NEXT, NEXT, (1090, 608)],
    "cell": [(900, 537), NEXT, NEXT, (1090, 608)],  # diet card, difficulty, planet name
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", choices=sorted(STAGE_ICON))
    ap.add_argument("-i", type=int, default=1)
    ap.add_argument("--slot", default="480,290", help="empty planet slot in the galaxy menu")
    ap.add_argument("--shots", action="store_true", help="screenshot every wizard step to out/shots")
    a = ap.parse_args()
    c = Console(a.i)
    c.cmd = lambda line, timeout=None: c.call(line).get("out", "")
    c.cmd("cheat levels -unlock")
    sx, sy = map(int, a.slot.split(","))
    c.cmd(f"click {sx} {sy}")
    c.cmd("wait.frames 150 60000")
    x, y = STAGE_ICON[a.stage]
    c.cmd(f"click {x} {y}")
    c.cmd("wait.frames 150 60000")
    for n, (x, y) in enumerate(STEPS[a.stage]):
        c.cmd(f"click {x} {y}")
        c.cmd("wait.frames 80 60000")
        if a.shots:
            subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "..", "sporectl.py"), "-i",
                            str(a.i), "shot", f"out/shots/wiz_{a.stage}_{n}.png", "--scale", "0.4"],
                           stdout=subprocess.DEVNULL)
    t0 = time.time()
    while time.time() - t0 < 300:
        mode = c.cmd("game.mode")
        if MODE[a.stage].lower() in mode.lower():
            print("in", mode.strip())
            c.cmd("wait.frames 600 120000")
            return 0
        time.sleep(2)
    print("FAILED: still", c.cmd("game.mode").strip())
    return 1


if __name__ == "__main__":
    sys.exit(main())
