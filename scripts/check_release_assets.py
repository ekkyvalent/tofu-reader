#!/usr/bin/env python3
"""Verify that release assets match the names the on-device OTA updater requests.

The device builds tofu-reader-<tag_name verbatim>-<device>.bin from the latest
release JSON, so a stripped "v" or the legacy crosspoint- prefix makes a build
invisible to OTA. Exit 0 only when every expected asset is present and non-empty.
"""

import argparse
import re
import sys
from pathlib import Path

DEVICES = [
    "x3-x4",
    "sticky",
    "x4pro",
    "x4c",
    "papermono",
    "metalio_eink4",
    "eego_a4",
    "picco",
]

TAG_RE = re.compile(r"^v?\d+\.\d+\.\d+(rc)?$")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True, help="release tag, e.g. v1.3.0")
    parser.add_argument(
        "directory",
        nargs="?",
        default="dist",
        help="directory holding the release assets (default: dist)",
    )
    args = parser.parse_args()

    if not TAG_RE.match(args.tag):
        print(f"error: invalid release tag {args.tag!r}; expected vX.Y.Z or vX.Y.Zrc", file=sys.stderr)
        return 1

    directory = Path(args.directory)
    if not directory.is_dir():
        print(f"error: not a directory: {directory}", file=sys.stderr)
        return 1

    problems = []
    assets = []
    for device in DEVICES:
        name = f"tofu-reader-{args.tag}-{device}.bin"
        path = directory / name
        if not path.is_file():
            problems.append(f"missing: {name}")
        elif path.stat().st_size == 0:
            problems.append(f"empty:   {name}")
        else:
            assets.append((name, path.stat().st_size))

    legacy = sorted(p.name for p in directory.glob("crosspoint-*.bin"))
    for name in legacy:
        problems.append(f"legacy name (device never asks for it): {name}")

    for name, size in assets:
        print(f"{name} ({size} bytes)")
    for problem in problems:
        print(problem, file=sys.stderr)

    if problems:
        print(f"FAIL: {len(assets)}/{len(DEVICES)} OTA assets match tag {args.tag}", file=sys.stderr)
        return 1

    print(f"OK: {len(assets)}/{len(DEVICES)} OTA assets match tag {args.tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
