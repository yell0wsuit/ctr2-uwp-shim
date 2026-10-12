#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-"$root/local/game"}
export WINEPREFIX=${WINEPREFIX:-"$root/local/wine-test"}
export WINEDEBUG=${WINEDEBUG:--all}
export MVK_CONFIG_LOG_LEVEL=${MVK_CONFIG_LOG_LEVEL:-0}
export CTR2SHIM_TRACE=${CTR2SHIM_TRACE:-1}
export CTR2SHIM_MODE=${CTR2SHIM_MODE:-shim}
export WINEDLLOVERRIDES="d3dcompiler_47=n,b;msvcr120_app,msvcp120_app,vccorlib120_app=n${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
wine=${WINE:-"$HOME/Applications/Wine Devel.app/Contents/Resources/wine/bin/wine"}
[[ -x "$wine" ]] || { echo 'Set WINE to your Wine executable.' >&2; exit 1; }
[[ -f "$game/Game.exe" && -f "$game/ctr2shim.dll" ]] || { echo 'Run setup.sh first.' >&2; exit 1; }
# A crash otherwise opens Wine's crash dialog, which keeps the process alive
# until closed and stalls the relaunch below; print the backtrace and exit.
"$wine" reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null 2>&1 || true
cd "$game"
# Wine 11 WoW64 sometimes kills the process inside wined3d's first OpenGL
# setup ("Exception frame is not in stack limits"). That startup death leaves
# "CreateDXGIFactory1 begin" without a result in the log; retry only that case.
status=0
for attempt in 1 2 3; do
  start=$(wc -l < ctr2shim.log 2>/dev/null || echo 0)
  "$wine" Game.exe && exit 0 || status=$?
  run=$(tail -n +$((start + 1)) ctr2shim.log 2>/dev/null || true)
  if grep -q 'CreateDXGIFactory1 begin' <<< "$run" && ! grep -q 'CreateDXGIFactory1 hr=' <<< "$run"; then
    echo "Wine graphics startup crashed (attempt $attempt/3); relaunching." >&2
    continue
  fi
  exit "$status"
done
exit "$status"
