#!/usr/bin/env bash
# Produce the files uploaded to a GitHub release.
#
# Copies the firmware images built for one PlatformIO environment into dist/ as
# tofu-reader-v<version>-<env>.bin and tofu-reader-v<version>-<env>-factory.bin,
# then prints their name, size and sha256 so they can be pasted into the release
# description. Nothing is built, uploaded, or sent to the GitHub API.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat <<'EOF'
Usage: scripts/package-release.sh [env] [--allow-dev]

  env          PlatformIO environment to package (default: x4pro)
  --allow-dev  Package a development version (contains "-dev") anyway
EOF
}

ENV_NAME="x4pro"
ALLOW_DEV=false
while (($#)); do
  case "$1" in
    --allow-dev)
      ALLOW_DEV=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --*)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      ENV_NAME="$1"
      shift
      ;;
  esac
done

ENV_NAME="$(printf '%s' "$ENV_NAME" | sed 's/[^A-Za-z0-9._-]/_/g')"

if ! REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null)"; then
  echo "error: $SCRIPT_DIR is not inside a git repository" >&2
  exit 1
fi

# Single source of truth: scripts/version.py derives the string from VERSION.
VERSION="$(python3 "$REPO_ROOT/scripts/version.py")"
VERSION="$(printf '%s' "$VERSION" | sed 's/[^A-Za-z0-9._+-]/_/g')"

if [[ "$VERSION" == *-dev* && "$ALLOW_DEV" != true ]]; then
  echo "error: $VERSION is not a release build" >&2
  echo "Tag the release commit (v<VERSION>) with a clean tree, or pass --allow-dev to package it anyway." >&2
  exit 1
fi

BUILD_DIR="$REPO_ROOT/.pio/build/$ENV_NAME"
APP_SRC="$BUILD_DIR/firmware.bin"
FACTORY_SRC="$BUILD_DIR/firmware.factory.bin"

for required in "$APP_SRC" "$FACTORY_SRC"; do
  if [[ ! -f "$required" ]]; then
    echo "error: required build artifact not found: $required" >&2
    echo "Run 'pio run -e $ENV_NAME' first." >&2
    exit 1
  fi
done

DIST_DIR="$REPO_ROOT/dist"
mkdir -p "$DIST_DIR"

# The OTA updater composes the asset name from the release tag exactly; this repo
# tags v<VERSION>, so the "v" must be part of the file name.
APP_DEST="$DIST_DIR/tofu-reader-v${VERSION}-${ENV_NAME}.bin"
FACTORY_DEST="$DIST_DIR/tofu-reader-v${VERSION}-${ENV_NAME}-factory.bin"

cp -f -- "$APP_SRC" "$APP_DEST"
cp -f -- "$FACTORY_SRC" "$FACTORY_DEST"

describe() {
  local path="$1"
  local size
  local sha
  size="$(stat -c %s -- "$path")"
  sha="$(sha256sum -- "$path" | cut -d' ' -f1)"
  echo "  name:   ${path##*/}"
  echo "  size:   $size bytes"
  echo "  sha256: $sha"
}

echo "Release assets for Tofu Reader v$VERSION ($ENV_NAME):"
echo
describe "$APP_DEST"
echo
describe "$FACTORY_DEST"
