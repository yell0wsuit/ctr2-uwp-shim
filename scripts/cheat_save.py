#!/usr/bin/env python3
"""Edit offline CTR2 shim saves. Close the game first; changes default to a preview.

The original game validates inventory using an MD5 of the preference name,
amount, and device ID. The shim supplies a fixed compatibility device token.
Shim JSON saves and the game's compressed config files are updated together.
Game.exe is never modified. Use this editor
with the original ctr2-wine-shim hardware token (including its trailing 00 01 bytes).
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import zlib

DEVICE_ID = b"ctr2-wine-shim\x00\x01".hex()
PREFIX = "com.zeptolab.ctr2.f2p."
ITEMS = ("coins", "balloons", "hints", "bombs", "teleports")
CONFIG_FILES = ("config.dat", "config1.dat", "config2.dat")


def inventory_hash(key, amount):
    """Compute the game's inventory checksum for the shim's device token."""
    text = f"{amount}!don'thackthis!{key}!{amount}!{DEVICE_ID}!ctr2."
    return hashlib.md5(text.encode("utf-8")).hexdigest()


def validate_record(record):
    """Validate the field order and string values expected by the shim reader."""
    if not isinstance(record, dict) or list(record) != ["key", "type", "value"]:
        raise ValueError(
            "Expected a typed settings record with key, type, value in that order."
        )
    if not all(isinstance(value, str) for value in record.values()):
        raise ValueError("Settings record fields must be strings.")
    return record


def load_store(folder):
    """Replay a snapshot and journal, returning records and both journal prefixes."""
    snapshot = folder / "settings.json"
    journal = folder / "settings.json.journal"
    records = {}
    if snapshot.exists():
        entries = json.loads(snapshot.read_text(encoding="utf-8-sig"))
        if not isinstance(entries, list):
            raise ValueError(f"{snapshot}: expected an array of settings records.")
        for record in entries:
            validate_record(record)
            records[record["key"]] = record
    original = journal.read_bytes() if journal.exists() else b""
    valid = b""
    # Match the shim: replay complete records only, stopping at a malformed tail.
    for line in original.splitlines(keepends=True):
        if not line.endswith(b"\n"):
            break
        try:
            record = validate_record(json.loads(line))
        except (ValueError, UnicodeError):
            break
        if record["type"] == "Clear":
            records.clear()
        elif record["type"] == "Remove":
            records.pop(record["key"], None)
        else:
            records[record["key"]] = record
        valid += line
    return records, original, valid


def prepare_change(folder, requested):
    """Validate existing hashes and prepare journal records for new balances."""
    records, original, valid = load_store(folder)
    changes = []
    for key, amount in ((PREFIX + item, value) for item, value in requested.items()):
        current = records.get(key)
        checksum = records.get(key + "_HASH")
        if not current or current["type"] not in ("Int32", "Int64") or not checksum:
            raise ValueError(
                f"{folder.name}: missing or unsupported {key} records; launch the game once first."
            )
        old_amount = int(current["value"])
        if checksum["type"] != "String" or checksum["value"] != inventory_hash(
            key, old_amount
        ):
            raise ValueError(
                f"{folder.name}: {key} hash does not match this shim; refusing to change it."
            )
        print(f"{folder.name}: {key} {old_amount} -> {amount}")
        changes.extend(
            [
                {"key": key, "type": current["type"], "value": str(amount)},
                {
                    "key": key + "_HASH",
                    "type": "String",
                    "value": inventory_hash(key, amount),
                },
            ]
        )
    # Field order matters to the shim's JSON reader. Appended records override the snapshot.
    encoded = "".join(
        json.dumps(record, separators=(",", ":")) + "\n" for record in changes
    ).encode("utf-8")
    return folder / "settings.json.journal", original, valid + encoded


def binary_string(data, offset):
    """Read the game's length-prefixed byte string and return its end offset."""
    if offset >= len(data):
        raise ValueError("Truncated config string length.")
    length = data[offset]
    offset += 1
    if length == 255:
        length = struct.unpack_from("<Q", data, offset)[0]
        offset += 8
    end = offset + length
    if end > len(data):
        raise ValueError("Truncated config string.")
    return data[offset:end], end


def decode_config(content):
    """Index typed preference values in a size-prefixed zlib config file."""
    raw = zlib.decompress(content[4:])
    if len(raw) != struct.unpack_from("<I", content)[0]:
        raise ValueError("Config decompressed length does not match its header.")
    count = struct.unpack_from("<Q", raw)[0]
    offset = 8
    records = {}
    for _ in range(count):
        key, offset = binary_string(raw, offset)
        kind = raw[offset]
        offset += 1
        start = offset
        if kind == 0:
            value, offset = binary_string(raw, offset)
        elif kind in (1, 2):
            value = struct.unpack_from("<q" if kind == 1 else "<d", raw, offset)[0]
            offset += 8
        else:
            raise ValueError(f"Unsupported config value type: {kind}")
        if key in records:
            raise ValueError("Duplicate config preference key.")
        records[key] = (kind, value, start, offset)
    if offset != len(raw):
        raise ValueError("Unexpected data after config preferences.")
    return raw, records


def prepare_config(path, requested):
    """Patch only requested balances and hashes, preserving other binary records."""
    original = path.read_bytes()
    raw, records = decode_config(original)
    updated = bytearray(raw)
    for item, amount in requested.items():
        key = PREFIX + item
        current = records.get(key.encode())
        checksum = records.get((key + "_HASH").encode())
        if not current or current[0] != 1 or not checksum or checksum[0] != 0:
            raise ValueError(f"{path.name}: missing or unsupported {key} records.")
        if checksum[1] != inventory_hash(key, current[1]).encode():
            raise ValueError(f"{path.name}: {key} hash does not match this shim.")
        # Integer values and MD5 strings have fixed sizes, keeping all offsets valid.
        digest = inventory_hash(key, amount).encode()
        if checksum[3] - checksum[2] != 1 + len(digest):
            raise ValueError(f"{path.name}: unsupported checksum string encoding.")
        updated[current[2] : current[3]] = struct.pack("<q", amount)
        updated[checksum[2] : checksum[3]] = bytes([len(digest)]) + digest
        print(f"{path.name}: {key} {current[1]} -> {amount}")
    content = struct.pack("<I", len(updated)) + zlib.compress(updated)
    return path, original, content


def replace_atomic(path, content):
    """Flush a temporary file before atomically replacing the destination."""
    fd, temporary = tempfile.mkstemp(
        prefix=path.name + ".", suffix=".tmp", dir=path.parent
    )
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def parse_amount(text):
    """Parse a nonnegative balance within the game's signed 32-bit range."""
    value = int(text)
    if not 0 <= value <= 2**31 - 1:
        raise argparse.ArgumentTypeError("Use an amount between 0 and 2147483647.")
    return value


def backup_stores(root, folders):
    """Copy existing snapshots and journals into a timestamped backup."""
    backup = (
        root
        / "cheat-backups"
        / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    )
    for folder in folders:
        target = backup / folder.name
        target.mkdir(parents=True, exist_ok=False)
        for filename in (
            "settings.json",
            "settings.json.journal",
            *CONFIG_FILES,
            "config_check.dat",
        ):
            source = folder / filename
            if source.exists():
                shutil.copy2(source, target / filename)
    return backup


def main():
    """Preview requested changes or back up and apply them to existing stores."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--save-dir",
        type=Path,
        required=True,
        help="save root containing Local and Roaming",
    )
    for item in ITEMS:
        parser.add_argument(
            "--" + item, type=parse_amount, help=f"set the {item} balance"
        )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="back up and apply changes; close the game first",
    )
    args = parser.parse_args()
    requested = {
        item: getattr(args, item) for item in ITEMS if getattr(args, item) is not None
    }
    if not requested:
        parser.error("Specify an inventory amount.")
    root = args.save_dir.resolve()
    folders = [
        root / name
        for name in ("Local", "Roaming")
        if any(
            (root / name / filename).is_file()
            for filename in ("settings.json", "settings.json.journal")
        )
    ]
    if not folders:
        parser.error(f"No shim JSON saves found in {root}.")
    try:
        plans = [prepare_change(folder, requested) for folder in folders]
        plans.extend(
            prepare_config(root / "Local" / filename, requested)
            for filename in CONFIG_FILES
            if (root / "Local" / filename).is_file()
        )
        if not args.apply:
            print(
                "Preview only. Close the game and add --apply to back up and write these changes."
            )
            return
        print(f"Backup: {backup_stores(root, folders)}")
        for path, original, content in plans:
            if (path.read_bytes() if path.exists() else b"") != original:
                raise ValueError(
                    "Save file changed during editing. Close the game and try again."
                )
            replace_atomic(path, content)
        print("Saved. Start the game to load the new balances.")
    except (
        OSError,
        ValueError,
        KeyError,
        IndexError,
        struct.error,
        zlib.error,
    ) as error:
        parser.exit(1, f"Error: {error}\n")


if __name__ == "__main__":
    main()
