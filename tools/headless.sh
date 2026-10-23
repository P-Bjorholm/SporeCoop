#!/usr/bin/env bash
# A headless GNOME Shell for the test games (docs/06-testing.md): nothing appears on the user's
# desktop and the games keep drawing. It has its own runtime, config and state folders and its own
# D-Bus session: sharing the real runtime folder lets it unmount the Flatpak document portal
# (/run/user/$UID/doc).
#
#   headless.sh up      start it (once) and print DISPLAY=... XAUTHORITY=...
#   headless.sh env     print the variables if it runs (exit 1 if not)
#   headless.sh down    stop it and everything it started (its process group)
#
# The display comes from the live Xwayland of this shell (its -auth file is in our runtime folder),
# never from a log: a stale value once sent test games to the user's desktop.
set -euo pipefail
RT="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/sporecoop-headless"
# one virtual monitor; SporeKit puts game windows in a 4x3 grid of 1440x1080 cells (instance
# 13+ reuse the first cells). Keep 5760x3240: Wine offers 1440x1080 on it (5760x4320 does not).
SIZE="${SPORE_HEADLESS_SIZE:-5760x3240}"

# "DISPLAY=:N XAUTHORITY=..." of the Xwayland that serves our shell, or nothing
envvars() {
    local p args disp auth
    for p in $(pgrep -x Xwayland || true); do
        args=$(tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null || true)
        [[ "$args" == *"-auth $RT/"* ]] || continue
        disp=$(awk '{print $2}' <<< "$args")
        auth=$(grep -oP -- '-auth \K\S+' <<< "$args")
        [[ "$disp" == :* && -S "/tmp/.X11-unix/X${disp#:}" && -f "$auth" ]] || continue
        echo "DISPLAY=$disp XAUTHORITY=$auth"
        return 0
    done
    return 1
}

group() { [[ -f "$RT/pgid" ]] && cat "$RT/pgid"; }

running() { local g; g=$(group) && [[ -n "$g" ]] && kill -0 -- "-$g" 2>/dev/null; }

case "${1:-}" in
    up)
        if envvars; then
            exit 0
        fi
        "$0" down >/dev/null 2>&1 || true
        mkdir -p "$RT"/{config,state,cache}
        chmod 700 "$RT"
        rm -f "$RT"/.mutter-Xwaylandauth.* "$RT/shell.log"
        # setsid: its own session and process group, so "down" stops all of it
        setsid dbus-run-session -- env -u WAYLAND_DISPLAY -u DISPLAY -u XAUTHORITY \
            XDG_RUNTIME_DIR="$RT" XDG_CONFIG_HOME="$RT/config" XDG_STATE_HOME="$RT/state" \
            XDG_CACHE_HOME="$RT/cache" GSETTINGS_BACKEND=keyfile \
            gnome-shell --headless --wayland --wayland-display=sporecoop-0 --virtual-monitor "$SIZE" \
            > "$RT/shell.log" 2>&1 < /dev/null &
        echo $! > "$RT/pgid"
        for _ in $(seq 1 60); do
            sleep 0.5
            envvars && exit 0
        done
        echo "headless shell did not start: $RT/shell.log" >&2
        exit 1
        ;;
    env) envvars ;;
    down)
        if running; then
            kill -- "-$(group)" 2>/dev/null || true
            sleep 1
            kill -9 -- "-$(group)" 2>/dev/null || true
        fi
        rm -f "$RT/pgid"
        ;;
    *) echo "usage: headless.sh up|env|down"; exit 1 ;;
esac
