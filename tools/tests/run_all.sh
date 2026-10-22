#!/usr/bin/env bash
# Run every co-op test (about 100 minutes). Results: out/tests/<name>.log + summary.
set -u
cd "$(dirname "$0")/../.."
mkdir -p out/tests
declare -a NAMES CMDS
add() { NAMES+=("$1"); CMDS+=("$2"); }
add unit       "make test"
add creature   "python3 tools/tests/coop_two_instances.py --guest-menu"
add cell       "python3 tools/tests/coop_mode.py --fixture cell --planet Game0/Karke"
add tribe      "python3 tools/tests/coop_mode.py --fixture tribe --planet Game0/Cohrach"
add civ        "python3 tools/tests/coop_mode.py --fixture civ --planet Game0/Agrauel"
add space      "python3 tools/tests/coop_mode.py --fixture space --planet Game0/Maraya"
add phase      "python3 tools/tests/coop_phase.py"
add ui         "python3 tools/tests/coop_ui.py"
add team       "python3 tools/tests/coop_team.py"
add tribe_coop "python3 tools/tests/coop_tribe.py"
add civ_coop   "python3 tools/tests/coop_civ.py"
add cell_coop  "python3 tools/tests/coop_cell.py"
add space_coop "python3 tools/tests/coop_space.py"
add vote       "python3 tools/tests/coop_vote.py"
add notify     "python3 tools/tests/coop_notify.py"
add sync       "python3 tools/tests/coop_sync.py"
summary=""
for i in "${!NAMES[@]}"; do
    n=${NAMES[$i]}
    echo "== $n: ${CMDS[$i]}"
    if timeout 3000 bash -c "${CMDS[$i]}" > "out/tests/$n.log" 2>&1 && ! grep -q "RESULT: FAIL" "out/tests/$n.log"; then
        r=OK
    else
        r=FAIL
    fi
    echo "   $r"
    summary+="$n=$r "
done
tools/spore.sh kill >/dev/null
echo "SUMMARY: $summary"
