#!/usr/bin/env bash
# SporeCoop helper: install loader, deploy builds, run/kill the game in Bottles, logs.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# test lanes (docs/10-dev-pipeline.md): lane 1 = the real install in the "Games" bottle; lane N>1 =
# its own bottle SporeLabN (own wineserver: a crash or quit there leaves the other lanes alone),
# its own game folder (own mods, logs, SporeCoop.package) and instances 2N-1 (host) / 2N (guest)
LANE="${SPORE_LANE:-1}"
MAIN_GAME="$(cd "$(dirname "$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)")/.." && pwd)"
LAB="${SPORE_LAB:-$(dirname "$MAIN_GAME")/SPORE-lab}"
if [[ "$LANE" == 1 ]]; then
    GAME="${SPORE_GAME:-$MAIN_GAME}"
    BOTTLE="${SPORE_BOTTLE:-Games}"
else
    GAME="${SPORE_GAME:-$LAB/lane$LANE}"
    BOTTLE="${SPORE_BOTTLE:-SporeLab$LANE}"
fi
HOST_INST=$((2 * LANE - 1))
BIN="$GAME/SporebinEP1"
MODS="$BIN/mods"
BACKUP="$ROOT/out/backup"
BOTTLES_DIR="$HOME/.var/app/com.usebottles.bottles/data/bottles/bottles"
# fixtures are shared by every worktree: they live in the main checkout
FIXTURES="${SPORE_FIXTURES:-$MAIN_GAME/SporeCoop/out/fixtures}"

bottles() { flatpak run --command=bottles-cli com.usebottles.bottles "$@"; }

is_ours() { strings "$1" 2>/dev/null | grep -q "sporekit-loader"; }

cmd_install() {
    mkdir -p "$BACKUP"
    if [[ ! -f "$BIN/steam_api_orig.dll" ]]; then
        if is_ours "$BIN/steam_api.dll"; then
            echo "error: steam_api.dll is already the loader but steam_api_orig.dll is missing" >&2
            exit 1
        fi
        cp -n "$BIN/steam_api.dll" "$BACKUP/steam_api.dll.gog"
        mv "$BIN/steam_api.dll" "$BIN/steam_api_orig.dll"
        echo "original steam_api.dll -> steam_api_orig.dll (backup in out/backup)"
    fi
    cp "$ROOT/build/steam_api.dll" "$BIN/steam_api.dll"
    echo "loader installed"
}

cmd_uninstall() {
    if [[ -f "$BIN/steam_api_orig.dll" ]]; then
        mv -f "$BIN/steam_api_orig.dll" "$BIN/steam_api.dll"
        rm -f "$BIN/../Data/SporeCoop.package"
        echo "original steam_api.dll restored (mods folder left in place)"
    else
        echo "loader not installed"
    fi
}

cmd_deploy() {
    mkdir -p "$MODS/sporekit/symbols" "$MODS/logs"
    [[ -f "$BIN/steam_api_orig.dll" ]] && cp "$ROOT/build/steam_api.dll" "$BIN/steam_api.dll"
    cp "$ROOT/build/sporekit.dll" "$BIN/sporekit.dll" 2>/dev/null || {
        echo "sporekit.dll is locked (game running?) - skipped" >&2; }
    for f in "$ROOT"/build/*.dll; do
        n=$(basename "$f")
        case "$n" in steam_api.dll|sporekit.dll) continue ;; esac
        cp "$f" "$MODS/$n"
    done
    cp "$ROOT"/data/symbols/*.sym "$MODS/sporekit/symbols/"
    # game resources: Spore loads every .package in its Data folder
    [[ -f "$ROOT/build/SporeCoop.package" ]] && cp "$ROOT/build/SporeCoop.package" "$BIN/../Data/SporeCoop.package"
    for ini in "$ROOT"/data/config/*.ini; do
        [[ -f "$ini" ]] || continue
        [[ -f "$MODS/$(basename "$ini")" ]] || cp "$ini" "$MODS/"
    done
    echo "deployed to $MODS"
}

APPDATA_DIR="$HOME/.var/app/com.usebottles.bottles/data/bottles/bottles/$BOTTLE/drive_c/users/$USER/AppData/Roaming"

# Test profiles live in AppData/Roaming/SporeKit-<name>/Spore and start from a copy of
# the player's preferences with windowed mode and without tutorials/hints.
prepare_profile() {
    local name="$1" size="${PROFILE_SCREEN_SIZE:-12}"
    local dst="$APPDATA_DIR/SporeKit-$name/Spore/Preferences"
    mkdir -p "$dst"
    local src="$APPDATA_DIR/Spore/Preferences"
    [[ -f "$dst/login.prop" || ! -f "$src/login.prop" ]] || cp "$src/login.prop" "$dst/"
    [[ -f "$dst/Preferences.prop" ]] || cp "$src/Preferences.prop" "$dst/" 2>/dev/null || touch "$dst/Preferences.prop"
    python3 - "$dst/Preferences.prop" "$size" <<'PY'
import sys
p, size = sys.argv[1], sys.argv[2]
want = {'OptionFullScreen': '0', 'OptionTutorialsEnabled': '0', 'OptionShowHints': '0',
        'OptionEdgeScroll': '0', 'OptionScreenSize': size,
        # test runs are silent (only the test profile, the player's own settings stay as they are)
        'AudioMuteAll': 'true', 'AudioMasterVolume': '0.000000'}
lines = open(p).read().splitlines()
seen = set()
out = []
for l in lines:
    k = l.split(' ', 1)[0]
    if k in want:
        l = k + ' ' + want[k]
        seen.add(k)
    out.append(l)
out += [k + ' ' + v for k, v in want.items() if k not in seen]
open(p, 'w').write('\n'.join(out) + '\n')
PY
}

# a [sporekit] setting in the lane's mods/sporekit.ini (created by deploy)
sk_ini_set() {
    local f="$MODS/sporekit.ini"
    [[ -f "$f" ]] || return 0
    if grep -q "^$1=" "$f"; then sed -i "s/^$1=.*/$1=$2/" "$f"; else sed -i "/^\[sporekit\]/a $1=$2" "$f"; fi
}

cmd_run() {
    local inst=1 profile=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -i) inst="$2"; shift 2 ;;
            -p) profile="$2"; shift 2 ;;
            *) inst="$1"; shift ;;
        esac
    done
    local args=()
    [[ "$inst" != "1" ]] && args+=("-sporekit-instance:$inst")
    if [[ -n "$profile" ]]; then
        prepare_profile "$profile"
        args+=("-sporekit-profile:$profile")
    elif [[ "$inst" != "1" ]]; then
        prepare_profile "inst$inst"
    fi
    mkdir -p "$ROOT/out"
    [[ -f "$BIN/SporeApp.exe" ]] || { echo "no game at $BIN (lane $LANE: spore.sh lane create $LANE)" >&2; exit 1; }
    nohup flatpak run --command=bottles-cli com.usebottles.bottles run -b "$BOTTLE" \
        -e "$BIN/SporeApp.exe" -- "${args[@]}" > "$ROOT/out/run.$inst.log" 2>&1 &
    # opt-in: a window on a hidden workspace gets no frame callbacks from GNOME, DXVK then waits
    # forever and the game stops at frame 1 (seen 2026-10-23); use the headless shell instead
    if [[ "${SPORE_AGENT_WS:-0}" == 1 ]] && command -v agent-ws >/dev/null; then
        # the game windows go to the "sporecoop" project's own workspace (agent-ws): Bottles drops
        # the environment that agent-ws launch would mark them with, so find them by process
        sk_ini_set test_monitor off
        nohup "$ROOT/tools/assign_windows.py" sporecoop "$(basename "$GAME")" "${args[*]:-}" > /dev/null 2>&1 &
    fi
    echo "started instance $inst ${profile:+profile $profile }(bottles log: out/run.$inst.log)"
}

# the Wine process command line starts with the Windows path, e.g. F:\...\SPORE\SporebinEP1\SporeApp.exe:
# only this lane's game folder matches
GAME_RE="^[A-Z]:\\\\.*\\\\$(basename "$GAME")\\\\SporebinEP1\\\\SporeApp\\.exe"

kill_tree() {
    local c
    for c in $(pgrep -P "$1"); do kill_tree "$c"; done
    kill -9 "$1" 2>/dev/null || true
}

cmd_kill() {
    pkill -f "$GAME_RE" || true
    sleep 1
    # a deadlocked Wine process can stay half-dead (zombie leader + live threads) and keep its
    # UDP/TCP sockets bound: SIGKILL the whole thread group
    pkill -9 -f "$GAME_RE" || true
    for z in $(ps -eo pid=,stat=,comm= | awk '$2 ~ /Z/ && $3 ~ /SporeApp/ {print $1}'); do
        kill -9 "$z" 2>/dev/null || true
    done
    # launch sandboxes keep a wine services tree alive after the game exits: kill the whole tree
    local w
    for w in $(ps -eo pid,comm,args | awk -v bin="$BIN/SporeApp.exe" '$2=="bwrap" && index($0, bin) {print $1}'); do
        kill_tree "$w"
    done
    # a test lane's bottle is ours alone: also whatever else ran in it (the setup in update tests
    # leaves Wine services behind). The "Games" bottle of lane 1 holds other games: only Spore there.
    if [[ "$LANE" != 1 ]]; then
        for w in $(ps -eo pid,comm,args | awk -v b="-b $BOTTLE " '$2=="bwrap" && index($0, b) {print $1}'); do
            kill_tree "$w"
        done
    fi
    # leftover launch wrappers (the [x] trick keeps pkill from matching this shell)
    pkill -f "[b]ottles-cli run -b $BOTTLE -e $BIN/SporeApp.exe" || true
    pkill -f "[s]tart.exe /exec start /wait /unix $BIN/SporeApp.exe" || true
    echo "killed"
}

cmd_status() {
    pgrep -af "$GAME_RE" || echo "not running"
}

cmd_logs() {
    local inst="${1:-1}"
    local f="$MODS/logs/sporekit.log"
    [[ "$inst" != "1" ]] && f="$MODS/logs/sporekit.$inst.log"
    tail -n "${LINES:-60}" "$f"
}

cmd_fixture() {
    local op="${1:-}" name="${2:-creature}" profile="${3:-test}"
    local prefix="$APPDATA_DIR/SporeKit-$profile/Spore"
    [[ "$profile" == "-" ]] && prefix="$APPDATA_DIR/Spore"
    mkdir -p "$FIXTURES" "$prefix"
    case "$op" in
        save) tar -C "$prefix" -czf "$FIXTURES/$name.tgz" Games && echo "saved fixture $name" ;;
        restore) rm -rf "$prefix/Games" && tar -C "$prefix" -xzf "$FIXTURES/$name.tgz" && echo "restored fixture $name" ;;
        slot)
            # warm reset: the fixture's Game0 goes into a new slot Game<slot> next to the loaded one
            # (files of the running save must not change under the game)
            local slot="${4:?slot number}" tmp
            tmp=$(mktemp -d "$prefix/.fixture.XXXXXX")
            tar -C "$tmp" -xzf "$FIXTURES/$name.tgz"
            mkdir -p "$prefix/Games"
            rm -rf "$prefix/Games/Game$slot"
            mv "$tmp/Games/Game0" "$prefix/Games/Game$slot"
            rm -rf "$tmp"
            echo "fixture $name in Game$slot" ;;
        *) echo "usage: spore.sh fixture save|restore [name] [profile|-] | slot name profile N"; exit 1 ;;
    esac
}

# a lane = reflink copies (Btrfs: no extra space until files change) of the game folder and of
# the bottle, with the game paths in the bottle registry pointed to the copy
cmd_lane() {
    local op="${1:-}" n="${2:-}"
    [[ "$n" =~ ^[0-9]+$ && "$n" -ge 2 ]] || { echo "usage: spore.sh lane create|remove|info N (N >= 2)"; exit 1; }
    local dir="$LAB/lane$n" bottle="$BOTTLES_DIR/SporeLab$n"
    case "$op" in
        create)
            if [[ ! -f "$dir/SporebinEP1/SporeApp.exe" ]]; then
                mkdir -p "$dir"
                local e
                for e in "$MAIN_GAME"/*; do
                    case "$(basename "$e")" in SporeCoop) continue ;; esac
                    cp -a --reflink=always "$e" "$dir/"
                done
                rm -rf "$dir/SporebinEP1/mods/logs"
            fi
            if [[ ! -f "$bottle/bottle.yml" ]]; then
                cp -a --reflink=always "$BOTTLES_DIR/Games" "$bottle"
                sed -i "s/^Name: .*/Name: SporeLab$n/; s/^Path: .*/Path: SporeLab$n/" "$bottle/bottle.yml"
                # the game paths in the registry: Z:\home\...\SPORE\ -> the lane copy
                local from to
                from=$(printf '%s' "Z:$MAIN_GAME" | sed 's|/|\\\\|g')
                to=$(printf '%s' "Z:$dir" | sed 's|/|\\\\|g')
                sed -i "s|$(printf '%s' "$from" | sed 's|\\|\\\\|g')\\\\|$(printf '%s' "$to" | sed 's|\\|\\\\|g')\\\\|g" \
                    "$bottle/system.reg" "$bottle/user.reg"
                # test profiles only: the player's own Spore data is not copied
                rm -rf "$bottle/drive_c/users/$USER/AppData/Roaming/Spore/Games"
            fi
            echo "lane $n: game $dir, bottle SporeLab$n, instances $((2 * n - 1))/$((2 * n))"
            ;;
        remove)
            SPORE_LANE=$n "$0" kill >/dev/null || true
            rm -rf "$dir" "$bottle"
            echo "lane $n removed"
            ;;
        info)
            echo "lane $n: game $dir ($( [[ -d $dir ]] && echo ok || echo missing)), bottle SporeLab$n ($( [[ -d $bottle ]] && echo ok || echo missing))"
            ;;
        *) echo "usage: spore.sh lane create|remove|info N"; exit 1 ;;
    esac
}

case "${1:-}" in
    lane) shift; cmd_lane "$@" ;;
    install) cmd_install ;;
    uninstall) cmd_uninstall ;;
    deploy) cmd_deploy ;;
    run) shift; cmd_run "$@" ;;
    kill) cmd_kill ;;
    status) cmd_status ;;
    logs) shift; cmd_logs "${1:-1}" ;;
    fixture) shift; cmd_fixture "$@" ;;
    *) echo "usage: [SPORE_LANE=N] spore.sh lane create|remove N|install|uninstall|deploy|run [-i inst] [-p profile]|kill|status|logs [inst]|fixture save|restore [name]"; exit 1 ;;
esac
