#!/usr/bin/env python3
"""
Bump the Tofu Reader version stored in the VERSION file.

VERSION holds one bare MAJOR.MINOR.PATCH line, with no "v" prefix; the release
tag is that value with a "v" in front. The same value must appear on the
`version = ` line of platformio.ini, because the release workflow rejects a tag
that disagrees with it. Bumps rewrite both files and print the git commands
needed to publish them:

  1.0.0 + patch -> 1.0.1
  1.0.0 + minor -> 1.1.0
  1.0.0 + major -> 2.0.0

Usage:
  scripts/bump_version.py [show | check | patch | minor | major [--dry-run]]
"""

import os
import re
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
VERSION_FILE = os.path.join(PROJECT_DIR, 'VERSION')
PIO_FILE = os.path.join(PROJECT_DIR, 'platformio.ini')
VERSION_PATTERN = re.compile(r'^[0-9]+\.[0-9]+\.[0-9]+$')
PIO_VERSION_PATTERN = re.compile(r'^version = (.*)$', re.MULTILINE)
BUMP_PARTS = ('patch', 'minor', 'major')

USAGE = """\
Usage: scripts/bump_version.py [show | check | patch | minor | major [--dry-run]]

  show       Print the versions in VERSION and platformio.ini (the default)
  check      Print the mismatch line and exit 1 when the two disagree
  patch      Bump the patch component: X.Y.Z -> X.Y.(Z+1)
  minor      Bump the minor component: X.Y.Z -> X.(Y+1).0
  major      Bump the major component: X.Y.Z -> (X+1).0.0
  --dry-run  Print old -> new without writing VERSION or platformio.ini
"""


def usage():
    sys.stderr.write(USAGE)


def load_version(path=VERSION_FILE):
    """Return the stripped version line, raising ValueError with a readable message."""
    try:
        with open(path, encoding='utf-8') as handle:
            raw = handle.read()
    except OSError as e:
        raise ValueError(f'could not read {path}: {e}')
    version = raw.strip()
    if not VERSION_PATTERN.match(version):
        raise ValueError(f'{path} contains {version!r}, expected MAJOR.MINOR.PATCH')
    return version


def load_pio_version(path=PIO_FILE):
    """Return the platformio.ini version, raising ValueError with a readable message."""
    try:
        with open(path, encoding='utf-8') as handle:
            raw = handle.read()
    except OSError as e:
        raise ValueError(f'could not read {path}: {e}')
    match = PIO_VERSION_PATTERN.search(raw)
    if not match:
        raise ValueError(f'{path} has no "version = " line')
    version = match.group(1).strip()
    if not VERSION_PATTERN.match(version):
        raise ValueError(f'{path} contains {version!r}, expected MAJOR.MINOR.PATCH')
    return version


def bump(version, part):
    """Return the next version for 'patch', 'minor' or 'major'."""
    if not VERSION_PATTERN.match(version):
        raise ValueError(f'{version!r} is not MAJOR.MINOR.PATCH')
    major, minor, patch = (int(piece) for piece in version.split('.'))
    if part == 'patch':
        patch += 1
    elif part == 'minor':
        minor, patch = minor + 1, 0
    elif part == 'major':
        major, minor, patch = major + 1, 0, 0
    else:
        raise ValueError(f'unknown bump part: {part!r}')
    return f'{major}.{minor}.{patch}'


def write_version(version, path=VERSION_FILE):
    """Write one version line with a trailing newline."""
    with open(path, 'w', encoding='utf-8') as handle:
        handle.write(version + '\n')


def write_pio_version(version, path=PIO_FILE):
    """Rewrite the first `version = ` line in platformio.ini."""
    with open(path, encoding='utf-8') as handle:
        raw = handle.read()
    updated, count = PIO_VERSION_PATTERN.subn(f'version = {version}', raw, count=1)
    if count != 1:
        raise ValueError(f'{path} has no "version = " line')
    with open(path, 'w', encoding='utf-8') as handle:
        handle.write(updated)


def print_versions(version, pio_version):
    """Print both sources and return 1 when they disagree."""
    print(f'{"VERSION":<15}{version}')
    print(f'{"platformio.ini":<15}{pio_version}')
    if version != pio_version:
        print(f'MISMATCH: VERSION says {version}, platformio.ini says {pio_version}',
              file=sys.stderr)
        return 1
    return 0


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)

    dry_run = '--dry-run' in args
    if dry_run:
        args.remove('--dry-run')

    if not args:
        args = ['show']
    if len(args) != 1:
        usage()
        return 2

    command = args[0]

    if command == 'show':
        if dry_run:
            usage()
            return 2
        try:
            version = load_version()
            pio_version = load_pio_version()
        except ValueError as e:
            print(f'error: {e}', file=sys.stderr)
            return 1
        return print_versions(version, pio_version)

    if command == 'check':
        if dry_run:
            usage()
            return 2
        try:
            version = load_version()
            pio_version = load_pio_version()
        except ValueError as e:
            print(f'error: {e}', file=sys.stderr)
            return 1
        if version != pio_version:
            print(f'MISMATCH: VERSION says {version}, platformio.ini says {pio_version}',
                  file=sys.stderr)
            return 1
        return 0

    if command not in BUMP_PARTS:
        usage()
        return 2

    try:
        old = load_version()
        pio_old = load_pio_version()
        new = bump(old, command)
    except ValueError as e:
        print(f'error: {e}', file=sys.stderr)
        return 1

    if dry_run:
        print(f'{"VERSION":<15}{old} -> {new}')
        print(f'{"platformio.ini":<15}{pio_old} -> {new}')
        return 0

    try:
        write_pio_version(new)
        write_version(new)
    except (OSError, ValueError) as e:
        print(f'error: could not write the version: {e}', file=sys.stderr)
        return 1

    print(f'{old} -> {new}')
    print(f'updated VERSION and platformio.ini (version = {new})')
    print('git add VERSION platformio.ini')
    print(f'git commit -m "chore(release): v{new}"')
    print(f'git tag v{new}')
    print('git push origin HEAD --tags')
    return 0


if __name__ == '__main__':
    sys.exit(main())
