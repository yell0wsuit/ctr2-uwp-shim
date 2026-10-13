#!/usr/bin/env bash
set -euo pipefail
# Usage: run.sh [GAME_DIRECTORY] [--trace] [--capture] [--diagnostics] [--passthrough]
root=$(cd "$(dirname "$0")/.." && pwd)
game="$root/local/game"
# --log always: the relaunch check below reads the log.
flags=(--log)
renderer=${CTR2_RENDERER:-opengl}
while [[ $# -gt 0 ]]; do
  case "$1" in
    --renderer) [[ $# -ge 2 ]] || { echo 'Missing renderer value.' >&2; exit 2; }; renderer=$2; shift ;;
    --renderer=*) renderer=${1#*=} ;;
    --*) flags+=("$1") ;;
    *) game=$1 ;;
  esac
  shift
done
case "$renderer" in
  opengl|vulkan) ;;
  *) echo "Unknown renderer '$renderer': use opengl or vulkan." >&2; exit 2 ;;
esac
export WINEPREFIX=${WINEPREFIX:-"$root/local/wine-test"}
export WINEDEBUG=${WINEDEBUG:--all}
export MVK_CONFIG_LOG_LEVEL=${MVK_CONFIG_LOG_LEVEL:-0}
overrides="d3dcompiler_47=n,b;msvcr120_app,msvcp120_app,vccorlib120_app=n"
case "$renderer" in
  opengl) overrides+=';d3d11,dxgi,wined3d=n' ;;
  vulkan) overrides+=';wined3d,d3d11,dxgi=b' ;;
esac
# Backend selection takes precedence over ambient overrides, including stale
# wined3d=n from the previous launcher.
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}$overrides"
wine=${WINE:-"$HOME/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine"}
[[ -x "$wine" ]] || { echo 'Set WINE to your Wine executable.' >&2; exit 1; }
[[ -f "$game/Game.exe" && -f "$game/ctr2shim.dll" ]] || { echo 'Run setup.sh first.' >&2; exit 1; }
if [[ "$renderer" == opengl ]]; then
  python3 - "$game" <<'CHECK_STACK'
import hashlib, json, pathlib, sys
game = pathlib.Path(sys.argv[1])
try:
    manifest = json.loads((game / 'wine-graphics.json').read_text())
    for name in ('d3d11.dll', 'dxgi.dll', 'wined3d.dll'):
        if hashlib.sha256((game / name).read_bytes()).hexdigest() != manifest['sha256'][name]:
            raise ValueError(f'{name} does not match the graphics stack')
except (OSError, ValueError, KeyError, TypeError) as error:
    sys.exit(f'Invalid Wine graphics stack: {error}. Build scripts/build-wined3d.sh and rerun setup.sh.')
CHECK_STACK
fi
# Per-application setting avoids changing other games in a shared prefix.
if [[ "$renderer" == vulkan ]]; then backend=vulkan; else backend=gl; fi
"$wine" reg add 'HKCU\Software\Wine\AppDefaults\Game.exe\Direct3D' /v renderer /t REG_SZ /d "$backend" /f > /dev/null
echo "Wine renderer: $renderer" >&2
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
