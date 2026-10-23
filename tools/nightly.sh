#!/usr/bin/env bash
# Nightly test run (docs/10-dev-pipeline.md): the nightly suite (full + long chaos + bad network)
# on 3 lanes for the main branch, then a desktop notification with the result.
# systemd user timer: tools/systemd/sporecoop-nightly.{service,timer}
set -u
cd "$(dirname "$0")/.."
log="out/lab/nightly-$(date +%Y%m%d).log"
mkdir -p out/lab
python3 tools/lab.py run nightly --lanes "${SPORE_LANES:-3}" --branch main > "$log" 2>&1
rc=$?
summary=$(tail -1 "$log")
command -v notify-send >/dev/null && notify-send -a SporeCoop "SporeCoop nightly: $([[ $rc == 0 ]] && echo passed || echo FAILED)" "$summary"
exit $rc
