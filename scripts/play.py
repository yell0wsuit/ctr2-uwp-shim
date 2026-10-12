#!/usr/bin/env python3
"""Start the staged game.

On Windows this runs Game.exe directly. Elsewhere it runs scripts/run.sh,
which starts the game under Wine (and always logs, for its crash relaunch).
"""
import argparse
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SWITCHES = {
    'log': 'write failures and notes to ctr2shim.log beside Game.exe',
    'trace': 'log every traced call as well (implies --log)',
    'capture': 'save the back buffer to ctr2-frame.bmp on the first and every 120th frame',
    'diagnostics': 'log access violations with stack details (implies --log)',
    'passthrough': "activate the operating system's classes and only log",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', type=pathlib.Path, default=ROOT / 'local' / 'game',
                        help='staged game folder (default: local/game)')
    parser.add_argument('--wait', action='store_true', help='wait for the game to exit and return its exit code')
    for name, text in SWITCHES.items():
        parser.add_argument(f'--{name}', action='store_true', help=text)
    args = parser.parse_args()
    game = args.game.resolve()
    flags = [f'--{name}' for name in SWITCHES if getattr(args, name)]
    if sys.platform == 'win32':
        exe = game / 'Game.exe'
        if not (exe.is_file() and (game / 'ctr2shim.dll').is_file()):
            sys.exit(f'Game.exe and ctr2shim.dll not found in {game}. Stage the game with scripts/setup.ps1 first.')
        process = subprocess.Popen([str(exe), *flags], cwd=game)
        return process.wait() if args.wait else 0
    return subprocess.call(['bash', str(ROOT / 'scripts' / 'run.sh'), str(game), *flags])


if __name__ == '__main__':
    sys.exit(main())
