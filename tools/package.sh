#!/usr/bin/env bash
# Build a distributable zip for players (Windows or Wine): out/dist/SporeCoop-<ver>.zip
# Contents are extracted into SporebinEP1 and installed with install.bat.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER="$(grep -oP 'define COOP_VERSION "\K[^"]+' "$ROOT/mods/coop/coop.cpp")"
STAGE="$ROOT/out/dist/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/SporeCoop/mods/sporekit/symbols"
cp "$ROOT/build/steam_api.dll" "$STAGE/SporeCoop/steam_api.sporekit.dll"
cp "$ROOT/build/sporekit.dll" "$STAGE/SporeCoop/sporekit.dll"
cp "$ROOT/build/coop.dll" "$STAGE/SporeCoop/mods/coop.dll"
cp "$ROOT"/data/symbols/*.sym "$STAGE/SporeCoop/mods/sporekit/symbols/"
cp "$ROOT"/data/config/*.ini "$STAGE/SporeCoop/mods/"
cp "$ROOT/build/SporeCoop.package" "$STAGE/SporeCoop/SporeCoop.package"  # menu icon (Spore loads Data\*.package)

cat > "$STAGE/install.bat" <<'EOF'
@echo off
rem SporeCoop installer: run from the SporebinEP1 folder of SPORE (GOG)
cd /d "%~dp0"
if not exist SporeApp.exe (
  echo Put install.bat and the SporeCoop folder into ...\SPORE\SporebinEP1 and run it again.
  pause
  exit /b 1
)
rem supported builds: GOG 2016 (922af7c7...), GOG 2024 (dc04aee5...) and Steam (3010a413..., the GOG 2024
rem code under Steam's DRM), INSTALL.txt section 2
set WANT1=922af7c7908af2614b9caca2ffb1ec2353442022fd124866105724acaa24c26e
set WANT2=dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37
set WANT3=3010a413f9a4a05ee9c4de24756c1c94c7fb218f70cad29bcf5bc0b4abfa2466
for /f "usebackq delims=" %%h in (`powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 SporeApp.exe).Hash.ToLower()" 2^>nul`) do set HAVE=%%h
if defined HAVE if /i not "%HAVE%"=="%WANT1%" if /i not "%HAVE%"=="%WANT2%" if /i not "%HAVE%"=="%WANT3%" (
  echo Note: this SporeApp.exe is not one of the builds SporeCoop supports.
  echo It has not been tested yet - see INSTALL.txt, section 2.
  echo.
)
if not exist steam_api_orig.dll (
  copy /y steam_api.dll steam_api_orig.dll >nul
)
copy /y SporeCoop\steam_api.sporekit.dll steam_api.dll >nul
copy /y SporeCoop\sporekit.dll sporekit.dll >nul
if not exist mods mkdir mods
xcopy /e /i /y /q SporeCoop\mods mods >nul
copy /y SporeCoop\SporeCoop.package ..\Data\SporeCoop.package >nul
echo SporeCoop installed. Start SPORE as usual.
echo Main menu: "Co-op" - host a game or join a friend. In game F9 opens the co-op window.
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
del /q ..\Data\SporeCoop.package 2>nul
echo SporeCoop removed (the mods folder was kept).
pause
EOF

cat > "$STAGE/README-SporeCoop.txt" <<EOF
SporeCoop $VER - co-op for every stage of SPORE (GOG "SPORE Collection" and Steam)
https://github.com/WoodyWizard/SporeCoop

Install: extract this zip into SPORE\\SporebinEP1 and run install.bat (step by step: INSTALL.txt).
Supported builds of SporebinEP1\\SporeApp.exe: GOG 2024 (sha256 dc04aee5...), Steam (3010a413...: the
same game code under Steam's DRM) and, until 0.7.x, GOG 2016 (922af7c7...).
Tested: GOG 2016 and 2024 on Linux (Wine), GOG 2024 on Windows 11, Steam on Linux (Proton); EA app and disc not tested
(the full list: INSTALL.txt, section 2). All players need the same game build.

Host: main menu -> Co-op -> name -> Host, then load or start a game. The window shows the
addresses for friends (over the internet forward UDP port 47810 or use a VPN such as
ZeroTier / Radmin VPN / Tailscale).
Guest: main menu -> Co-op -> name -> click the host under "Games on your network" or type
its address -> Join. The host's world arrives and loads by itself.
In game F9 opens the lobby: players, stage, ping, ready, chat, "Send my world", "Leave".
Evolution (editor, next stage) is decided by a vote: everybody presses "I'm ready".
Cheat console (Ctrl+Shift+C) works too: coop host | coop join <ip> | coop leave.
EOF
# the toolchain the Makefile uses (LLVM_MINGW ?= ... there), unless set in the environment
LLVM_MINGW="${LLVM_MINGW:-$(sed -n 's/^LLVM_MINGW ?= //p' "$ROOT/Makefile" | sed 's/\$(HOME)/$HOME/; s|\$HOME|'"$HOME"'|')}"
STRIP="$LLVM_MINGW/bin/llvm-strip"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
cp "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/THIRD_PARTY_NOTICES.txt"
cp "$ROOT/third_party/minhook/LICENSE.txt" "$STAGE/SporeCoop/MinHook-LICENSE.txt"
cp "$ROOT/docs/INSTALL.md" "$STAGE/INSTALL.txt"
"$STRIP" "$STAGE/SporeCoop/sporekit.dll" "$STAGE/SporeCoop/mods/coop.dll" "$STAGE/SporeCoop/steam_api.sporekit.dll"
# the update pack (in-game updates and the setup) and the setup itself, also next to the zip
python3 "$ROOT/tools/mkpack.py" "$STAGE" "$VER" "$STAGE/SporeCoop-$VER.pack" >/dev/null
cp "$ROOT/build/SporeCoopSetup.exe" "$STAGE/SporeCoopSetup.exe"
"$STRIP" "$STAGE/SporeCoopSetup.exe"
cp "$STAGE/SporeCoop-$VER.pack" "$STAGE/SporeCoopSetup.exe" "$ROOT/out/dist/"
sed -i 's/\r*$/\r/' "$STAGE"/*.bat "$STAGE"/*.txt  # Windows line endings for cmd.exe and Notepad
(cd "$STAGE" && rm -f "../SporeCoop-$VER.zip" && zip -qr "../SporeCoop-$VER.zip" .)
echo "$ROOT/out/dist/SporeCoop-$VER.zip"
