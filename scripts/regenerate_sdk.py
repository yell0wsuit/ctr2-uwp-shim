#!/usr/bin/env python3
"""Regenerate checked-in exact ABI defaults from a Windows SDK winrt directory."""
import argparse
import json
import pathlib
from gen_stubs import generate

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk_winrt', type=pathlib.Path)
    args = parser.parse_args()
    output = pathlib.Path(__file__).resolve().parents[1] / 'shim' / 'generated'
    manifest = json.loads((output / 'interfaces.json').read_text())
    for name, contract in manifest.items():
        source = args.sdk_winrt / contract['header']
        text = generate(source.read_text(encoding='utf-8-sig'), source.name, contract['interfaces'])
        (output / name).write_text(text)

if __name__ == '__main__':
    main()
