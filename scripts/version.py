#!/usr/bin/env python3
"""
Print the Tofu Reader version string used by builds and local publishing.

The base version comes from the VERSION file at the repository root. A release
build (HEAD is the v<VERSION> tag and the working tree is clean) prints the
bare version, e.g. 1.0.0. Everything else is a development build:
  1.0.0-dev+<short SHA>
  1.0.0-dev+dirty+<short SHA>   (working tree has changes)
"""

import os
import subprocess
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
VERSION_FILE = os.path.join(PROJECT_DIR, 'VERSION')
UNKNOWN_VERSION = '0.0.0-unknown'


def warn(msg):
    print(f'WARNING [version.py]: {msg}', file=sys.stderr)


def read_base_version():
    try:
        with open(VERSION_FILE, encoding='utf-8') as handle:
            base = handle.read().strip()
    except OSError as e:
        warn(f'could not read {VERSION_FILE}: {e}; base version will be "0.0.0"')
        return '0.0.0'
    if not base:
        warn(f'{VERSION_FILE} is empty; base version will be "0.0.0"')
        return '0.0.0'
    return base


def run_git(args, label):
    """Return stripped stdout, or None when git is unavailable or the command fails."""
    try:
        result = subprocess.run(
            ['git', *args],
            text=True, capture_output=True, cwd=PROJECT_DIR
        )
    except FileNotFoundError:
        warn(f'git not found on PATH; cannot determine {label}')
        return None
    except OSError as e:
        warn(f'OS error reading git {label}: {e}')
        return None
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def main():
    base = read_base_version()
    short_sha = run_git(['rev-parse', '--short', 'HEAD'], 'short SHA')
    if not short_sha:
        return UNKNOWN_VERSION

    status = run_git(['status', '--porcelain', '--untracked-files=normal'], 'working tree status')
    if status is None:
        return UNKNOWN_VERSION
    dirty = bool(status)

    if not dirty:
        tag = run_git(['describe', '--tags', '--exact-match', '--match', 'v*'], 'release tag')
        if tag == f'v{base}':
            return base

    suffix = '-dev+dirty' if dirty else '-dev'
    return f'{base}{suffix}+{short_sha}'


if __name__ == '__main__':
    print(main())
