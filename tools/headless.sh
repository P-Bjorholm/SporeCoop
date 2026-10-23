#!/usr/bin/env bash
# A headless GNOME Shell for the test games (docs/06-testing.md): nothing appears on the user's
# desktop and the games keep drawing (a window on a hidden workspace of the real desktop stops at
# frame 1). It has its own runtime, config and state folders and its own D-Bus session: sharing the
# real runtime folder lets it unmount the Flatpak document portal (/run/user/$UID/doc).
#
#   headless.sh up      start it (once) and print DISPLAY=... XAUTHORITY=...
#   headless.sh env     print the variables if it runs
#   headless.sh down    stop it
set -euo pipefail
RT="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/sporecoop-headless"
# one big virtual monitor: up to 12 game windows of 1440x1080 side by side (SporeKit places them
# by instance number; covered windows would get no frames either)
SIZE="${SPORE_HEADLESS_SIZE:-5760x3240}"

running() { [[ -f "$RT/pid" ]] && kill -0 "$(cat "$RT/pid")" 2>/dev/null; }

envvars() {
    local disp auth
    disp=$(grep -oP 'Using public X11 display \K:[0-9]+' "$RT/shell.log" 2>/dev/null | tail -1 || true)
    auth=$(ls "$RT"/.mutter-Xwaylandauth.* 2>/dev/null | head -1 || true)
    [[ -n "$disp" && -n "$auth" ]] || return 1
    echo "DISPLAY=$disp XAUTHORITY=$auth"
}

case "${1:-}" in
    up)
        if ! running; then
            mkdir -p "$RT"/{config,state,cache}
            chmod 700 "$RT"
            rm -f "$RT"/.mutter-Xwaylandauth.* "$RT/shell.log"
            setsid nohup dbus-run-session -- env -u WAYLAND_DISPLAY -u DISPLAY -u XAUTHORITY \
                XDG_RUNTIME_DIR="$RT" XDG_CONFIG_HOME="$RT/config" XDG_STATE_HOME="$RT/state" \
                XDG_CACHE_HOME="$RT/cache" GSETTINGS_BACKEND=keyfile \
                gnome-shell --headless --wayland --wayland-display=sporecoop-0 --virtual-monitor "$SIZE" \
                > "$RT/shell.log" 2>&1 < /dev/null &
            echo $! > "$RT/pid"
            # Xwayland comes up with the first X client; poke it so its display is known
            for _ in $(seq 1 60); do
                sleep 0.5
                grep -q "Using public X11 display" "$RT/shell.log" 2>/dev/null && break
                grep -q "Xwayland" "$RT/shell.log" 2>/dev/null || true
            done
        fi
        envvars || { echo "headless shell did not start: $RT/shell.log" >&2; exit 1; }
        ;;
    env) running && envvars ;;
    down)
        running && kill "$(cat "$RT/pid")" 2>/dev/null || true
        rm -f "$RT/pid"
        ;;
    *) echo "usage: headless.sh up|env|down"; exit 1 ;;
esac
