#!/usr/bin/env bash
# Build a distributable zip for players (Windows or Wine): out/dist/SporeCoop-<ver>.zip
# Contents are extracted into SporebinEP1 and installed with install.bat.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER="$(grep -oP 'SK_VERSION "\K[^"]+' "$ROOT/sporekit/core.h")"
STAGE="$ROOT/out/dist/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/SporeCoop/mods/sporekit/symbols"
cp "$ROOT/build/steam_api.dll" "$STAGE/SporeCoop/steam_api.sporekit.dll"
cp "$ROOT/build/sporekit.dll" "$STAGE/SporeCoop/sporekit.dll"
cp "$ROOT/build/coop.dll" "$STAGE/SporeCoop/mods/coop.dll"
cp "$ROOT"/data/symbols/*.sym "$STAGE/SporeCoop/mods/sporekit/symbols/"
cp "$ROOT"/data/config/*.ini "$STAGE/SporeCoop/mods/"

cat > "$STAGE/install.bat" <<'EOF'
@echo off
rem SporeCoop installer: run from the SporebinEP1 folder of SPORE (GOG)
cd /d "%~dp0"
if not exist SporeApp.exe (
  echo Put install.bat and the SporeCoop folder into ...\SPORE\SporebinEP1 and run it again.
  pause
  exit /b 1
)
if not exist steam_api_orig.dll (
  copy /y steam_api.dll steam_api_orig.dll >nul
)
copy /y SporeCoop\steam_api.sporekit.dll steam_api.dll >nul
copy /y SporeCoop\sporekit.dll sporekit.dll >nul
if not exist mods mkdir mods
xcopy /e /i /y /q SporeCoop\mods mods >nul
echo SporeCoop installed. Start SPORE as usual.
echo In game: Ctrl+Shift+C, then "coop host" or "coop join <ip>".
pause
EOF

cat > "$STAGE/uninstall.bat" <<'EOF'
@echo off
cd /d "%~dp0"
if exist steam_api_orig.dll (
  copy /y steam_api_orig.dll steam_api.dll >nul
  del steam_api_orig.dll
)
del /q sporekit.dll 2>nul
echo SporeCoop removed (the mods folder was kept).
pause
EOF

cat > "$STAGE/README-SporeCoop.txt" <<EOF
SporeCoop $VER - co-op for SPORE Creature Stage (GOG "SPORE Collection" build)

Install: copy install.bat and the SporeCoop folder into SPORE\\SporebinEP1, run install.bat.
Requires the same game build as the host (GOG SporebinEP1\\SporeApp.exe,
sha256 922af7c7...). Other builds load the game normally but co-op stays disabled.

In game open the cheat console with Ctrl+Shift+C:
  coop name <nick>        your nickname
  coop host [port]        host (UDP 47810 by default; forward this port or use a VPN)
  coop join <ip[:port]>   join a host
  coop status | say <text> | leave
Host: save the game before hosting - the save is sent to joining guests automatically.
Guest: after "world ... saved as GameN", return to the main menu and load that planet,
then join again.
EOF
STRIP="${LLVM_MINGW:-$HOME/.local/opt/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64}/bin/llvm-strip"
"$STRIP" "$STAGE/SporeCoop/sporekit.dll" "$STAGE/SporeCoop/mods/coop.dll" "$STAGE/SporeCoop/steam_api.sporekit.dll"
(cd "$STAGE" && rm -f "../SporeCoop-$VER.zip" && zip -qr "../SporeCoop-$VER.zip" .)
echo "$ROOT/out/dist/SporeCoop-$VER.zip"
