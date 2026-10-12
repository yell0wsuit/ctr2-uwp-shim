#!/usr/bin/env bash
set -euo pipefail
# Usage: run.sh [GAME_DIRECTORY] [--trace] [--capture] [--diagnostics] [--passthrough]
root=$(cd "$(dirname "$0")/.." && pwd)
game="$root/local/game"
# --log always: the relaunch check below reads the log.
flags=(--log)
for arg in "$@"; do
  case "$arg" in
    --*) flags+=("$arg") ;;
    *) game=$arg ;;
  esac
done
export WINEPREFIX=${WINEPREFIX:-"$root/local/wine-test"}
export WINEDEBUG=${WINEDEBUG:--all}
export MVK_CONFIG_LOG_LEVEL=${MVK_CONFIG_LOG_LEVEL:-0}
overrides="d3dcompiler_47=n,b;msvcr120_app,msvcp120_app,vccorlib120_app=n"
# The patched wined3d.dll from build-wined3d.sh, when staged: without it Wine
# rejects every shader the game compiles and draws nothing.
[[ ! -f "$game/wined3d.dll" ]] || overrides+=";wined3d=n"
export WINEDLLOVERRIDES="$overrides${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
wine=${WINE:-"$HOME/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine"}
[[ -x "$wine" ]] || { echo 'Set WINE to your Wine executable.' >&2; exit 1; }
[[ -f "$game/Game.exe" && -f "$game/ctr2shim.dll" ]] || { echo 'Run setup.sh first.' >&2; exit 1; }
# A crash otherwise opens Wine's crash dialog, which keeps the process alive
# until closed and stalls the relaunch below; print the backtrace and exit.
"$wine" reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null 2>&1 || true
# Retina mode gives Windows programs the display's real pixels (otherwise macOS
# stretches the picture 2x), and 192 DPI keeps the window its usual size.
# CTR2_RETINA=0 turns this off. Wine reads both when its display processes
# start, so a change restarts the prefix's wineserver once.
retina=${CTR2_RETINA:-1}
marker="$WINEPREFIX/.ctr2-retina"
if [[ "$(cat "$marker" 2>/dev/null)" != "$retina" ]]; then
  if [[ "$retina" == 0 ]]; then mode=n dpi=96; else mode=y dpi=192; fi
  "$wine" reg add 'HKCU\Software\Wine\Mac Driver' /v RetinaMode /t REG_SZ /d "$mode" /f > /dev/null 2>&1
  "$wine" reg add 'HKCU\Control Panel\Desktop' /v LogPixels /t REG_DWORD /d "$dpi" /f > /dev/null 2>&1
  "$(dirname "$wine")/wineserver" -k 2> /dev/null || true
  "$(dirname "$wine")/wineserver" -w 2> /dev/null || true
  echo "$retina" > "$marker"
fi
cd "$game"
# Wine 11 WoW64 sometimes kills the process inside wined3d's first OpenGL
# setup ("Exception frame is not in stack limits"). That startup death leaves
# "CreateDXGIFactory1 begin" without a result in the log; retry only that case.
status=0
for attempt in 1 2 3; do
  "$wine" Game.exe "${flags[@]}" && exit 0 || status=$?
  # Wine launches always log, and each logging run starts a new ctr2shim.log.
  run=$(cat ctr2shim.log 2>/dev/null || true)
  if grep -q 'CreateDXGIFactory1 begin' <<< "$run" && ! grep -q 'CreateDXGIFactory1 hr=' <<< "$run"; then
    echo "Wine graphics startup crashed (attempt $attempt/3); relaunching." >&2
    continue
  fi
  exit "$status"
done
exit "$status"
