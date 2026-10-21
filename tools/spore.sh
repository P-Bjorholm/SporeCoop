#!/usr/bin/env bash
# SporeCoop helper: install loader, deploy builds, run/kill the game in Bottles, logs.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME="${SPORE_GAME:-$(cd "$ROOT/.." && pwd)}"
BIN="$GAME/SporebinEP1"
MODS="$BIN/mods"
BOTTLE="${SPORE_BOTTLE:-Games}"
BACKUP="$ROOT/out/backup"

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
        'OptionEdgeScroll': '0', 'OptionScreenSize': size}
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
    nohup flatpak run --command=bottles-cli com.usebottles.bottles run -b "$BOTTLE" \
        -e "$BIN/SporeApp.exe" -- "${args[@]}" > "$ROOT/out/run.$inst.log" 2>&1 &
    echo "started instance $inst ${profile:+profile $profile }(bottles log: out/run.$inst.log)"
}

# the Wine process command line starts with the Windows path, e.g. F:\...\SporeApp.exe
GAME_RE='^[A-Z]:\\.*SporeApp\.exe'

cmd_kill() {
    pkill -f "$GAME_RE" || true
    sleep 1
    # a deadlocked Wine process can stay half-dead (zombie leader + live threads) and keep its
    # UDP/TCP sockets bound: SIGKILL the whole thread group
    pkill -9 -f "$GAME_RE" || true
    for z in $(ps -eo pid=,stat=,comm= | awk '$2 ~ /Z/ && $3 ~ /SporeApp/ {print $1}'); do
        kill -9 "$z" 2>/dev/null || true
    done
    pkill -f "[b]wrap --args [0-9]* -- bottles-cli run -b $BOTTLE -e $BIN/SporeApp.exe" || true
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
    mkdir -p "$ROOT/out/fixtures"
    case "$op" in
        save) tar -C "$prefix" -czf "$ROOT/out/fixtures/$name.tgz" Games && echo "saved fixture $name" ;;
        restore) tar -C "$prefix" -xzf "$ROOT/out/fixtures/$name.tgz" && echo "restored fixture $name" ;;
        *) echo "usage: spore.sh fixture save|restore [name] [profile|-]"; exit 1 ;;
    esac
}

case "${1:-}" in
    install) cmd_install ;;
    uninstall) cmd_uninstall ;;
    deploy) cmd_deploy ;;
    run) shift; cmd_run "$@" ;;
    kill) cmd_kill ;;
    status) cmd_status ;;
    logs) shift; cmd_logs "${1:-1}" ;;
    fixture) shift; cmd_fixture "$@" ;;
    *) echo "usage: spore.sh install|uninstall|deploy|run [-i inst] [-p profile]|kill|status|logs [inst]|fixture save|restore [name]"; exit 1 ;;
esac
