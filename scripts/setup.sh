#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo 'usage: setup.sh UNPACKED_GAME_DIRECTORY DESTINATION [BUILD_DIRECTORY]' >&2; exit 2
fi
source_dir=$(cd "$1" && pwd -P)
python3 - "$source_dir" "$2" <<'CHECK_PATHS'
import pathlib, sys
source, destination = (pathlib.Path(p).resolve() for p in sys.argv[1:])
if source == destination or source in destination.parents or destination in source.parents:
    sys.exit('Source and destination must not overlap.')
if destination.exists() and any(p.is_symlink() for p in destination.rglob('*')):
    sys.exit('Destination contains symbolic links; use a separate clean directory.')
CHECK_PATHS
mkdir -p "$2"
destination=$(cd "$2" && pwd)
build=${3:-"$root/build/macos-x86"}
[[ "$source_dir" != "$destination" ]] || { echo 'Use a separate destination; source game files are preserved.' >&2; exit 2; }
for name in Game.exe msvcr120_app.dll msvcp120_app.dll vccorlib120_app.dll; do
  [[ -f "$source_dir/$name" ]] || { echo "Missing $source_dir/$name (supply native x86 VCLibs 120 DLLs)" >&2; exit 1; }
done
for name in ctr2shim.dll d3dcompiler_47.dll; do
  [[ -f "$build/$name" ]] || { echo "Build output missing: $build/$name" >&2; exit 1; }
done
# Copy only runtime files; never the decompile, keys or certificates.
for name in Game.exe msvcr120_app.dll msvcp120_app.dll vccorlib120_app.dll App.xbf MainPage.xbf MovieMgrPage.xbf VideoPlayerPage.xbf resources.pri ZF.winmd VungleSDK.winmd; do
  [[ ! -f "$source_dir/$name" ]] || cp -p "$source_dir/$name" "$destination/"
done
[[ -d "$source_dir/Assets" ]] && cp -R "$source_dir/Assets" "$destination/"
# A raw .appx extraction keeps OPC percent-encoded names (%21_MasterGameDesignDocument.xml).
# The installer decodes them, and the game opens the decoded names; missing design
# XML leaves the game without its candy definition and crashes it after loading.
python3 - "$destination/Assets" <<'DECODE_NAMES'
import os, sys, urllib.parse
for parent, directories, files in os.walk(sys.argv[1], topdown=False):
    for name in files + directories:
        decoded = urllib.parse.unquote(name)
        if decoded == name:
            continue
        if '/' in decoded or '\0' in decoded:
            sys.exit(f'Cannot decode asset name: {os.path.join(parent, name)}')
        os.replace(os.path.join(parent, name), os.path.join(parent, decoded))
DECODE_NAMES
# Settings template, copied once so later edits survive re-staging.
[[ -f "$destination/ctr2shim.ini" ]] || cp "$root/scripts/ctr2shim.ini" "$destination/"
python3 - "$build" "$destination" <<'COPY_SHIMS'
import os, pathlib, shutil, sys, tempfile
build, destination = map(pathlib.Path, sys.argv[1:])
for name in ('ctr2shim.dll', 'd3dcompiler_47.dll'):
    fd, temporary = tempfile.mkstemp(prefix=name + '.', dir=destination)
    os.close(fd)
    try:
        shutil.copy2(build / name, temporary)
        os.replace(temporary, destination / name)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
COPY_SHIMS
cmp -s "$source_dir/Game.exe" "$destination/Game.exe"
shasum -a 256 "$source_dir/Game.exe" "$destination/Game.exe"
printf 'Prepared %s\n' "$destination"
