#!/usr/bin/env bash
# Publish the local firmware build to the tailnet download page.
# Copies the build artifacts into versioned names and writes the manifest that
# web/download/index.html reads. No build, no nginx changes, no sudo.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat <<'EOF'
Usage: scripts/publish-local.sh [env] [--keep N]

  env        PlatformIO environment to publish (default: x4pro)
  --keep N   Number of versioned files to keep per role (default: 5)
EOF
}

ENV_NAME="x4pro"
KEEP=5
while (($#)); do
  case "$1" in
    --keep)
      if (($# < 2)); then
        echo "error: --keep requires a number" >&2
        exit 2
      fi
      KEEP="$2"
      shift 2
      ;;
    --keep=*)
      KEEP="${1#*=}"
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

if ! [[ "$KEEP" =~ ^[0-9]+$ ]] || ((KEEP < 1)); then
  echo "error: --keep must be a positive integer" >&2
  exit 2
fi

ENV_NAME="$(printf '%s' "$ENV_NAME" | sed 's/[^A-Za-z0-9._-]/_/g')"

if ! REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null)"; then
  echo "error: $SCRIPT_DIR is not inside a git repository" >&2
  exit 1
fi

ROOT="${TOFU_DOWNLOAD_ROOT:-/srv/tofu-reader}"
WWW="$ROOT/www"
DL="$ROOT/dl"
BUILD_DIR="$REPO_ROOT/.pio/build/$ENV_NAME"

if ! mkdir -p "$WWW" "$DL" 2>/dev/null; then
  echo "error: could not create $WWW and $DL" >&2
  echo "Fix ownership of $ROOT so this user can write to it." >&2
  exit 1
fi
if [[ ! -w "$WWW" || ! -w "$DL" ]]; then
  echo "error: $ROOT is not writable" >&2
  echo "Fix ownership of $ROOT so this user can write to $WWW and $DL." >&2
  exit 1
fi

APP_SRC="$BUILD_DIR/firmware.bin"
FACTORY_SRC="$BUILD_DIR/firmware.factory.bin"
BOOTLOADER_SRC="$BUILD_DIR/bootloader.bin"
PARTITIONS_SRC="$BUILD_DIR/partitions.bin"

if [[ ! -f "$APP_SRC" ]]; then
  echo "error: required build artifact not found: $APP_SRC" >&2
  echo "Build env $ENV_NAME first, then run this script again." >&2
  exit 1
fi

for optional in "$FACTORY_SRC" "$BOOTLOADER_SRC" "$PARTITIONS_SRC"; do
  if [[ ! -f "$optional" ]]; then
    echo "warning: missing $optional, publishing without it" >&2
  fi
done

dirty_source="$(git -C "$REPO_ROOT" status --porcelain --untracked-files=normal -- src lib 2>/dev/null || true)"
source_commit_ts="$(git -C "$REPO_ROOT" log -1 --format=%ct -- src lib 2>/dev/null || true)"
bin_mtime="$(stat -c %Y -- "$APP_SRC" 2>/dev/null || true)"
stale_reason=""
if [[ -n "$dirty_source" ]]; then
  stale_reason="uncommitted changes in src or lib"
elif [[ "$source_commit_ts" =~ ^[0-9]+$ && "$bin_mtime" =~ ^[0-9]+$ ]] && ((source_commit_ts > bin_mtime)); then
  stale_reason="a commit touching src or lib"
fi
if [[ -n "$stale_reason" ]]; then
  echo "warning: $APP_SRC predates $stale_reason" >&2
  echo "warning: the published binary may not match the current source; rebuild before publishing." >&2
fi

if ! VERSION="$(git -C "$REPO_ROOT" describe --tags --always --dirty 2>/dev/null)"; then
  VERSION="$(git -C "$REPO_ROOT" rev-parse --short HEAD)"
  if ! git -C "$REPO_ROOT" diff --quiet HEAD -- 2>/dev/null; then
    VERSION="${VERSION}-dirty"
  fi
fi
VERSION="$(printf '%s' "$VERSION" | sed 's/[^A-Za-z0-9._-]/_/g')"

BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD)"
COMMIT="$(git -C "$REPO_ROOT" rev-parse --short HEAD)"
COMMIT_FULL="$(git -C "$REPO_ROOT" rev-parse HEAD)"
if [[ -n "$(git -C "$REPO_ROOT" status --porcelain --untracked-files=no)" ]]; then
  DIRTY="true"
else
  DIRTY="false"
fi

INDEX_SRC="$REPO_ROOT/web/download/index.html"
if [[ ! -f "$INDEX_SRC" ]]; then
  echo "error: missing page source: $INDEX_SRC" >&2
  exit 1
fi

PUBLISHED=()

publish_file() {
  local label="$1"
  local src="$2"
  local suffix="$3"
  local dest="$DL/tofu-reader-${VERSION}-${ENV_NAME}-${suffix}"
  [[ -f "$src" ]] || return 0
  cp -f -- "$src" "$dest"
  chmod 644 "$dest"
  PUBLISHED+=("${label}: ${dest##*/}")
}

publish_file "App image" "$APP_SRC" "app.bin"
publish_file "Factory image" "$FACTORY_SRC" "factory.bin"
publish_file "Bootloader" "$BOOTLOADER_SRC" "bootloader.bin"
publish_file "Partition table" "$PARTITIONS_SRC" "partitions.bin"

# Stable aliases used by the browser flasher.
cp -f -- "$APP_SRC" "$DL/firmware.bin"
chmod 644 "$DL/firmware.bin"
PUBLISHED+=("Browser flasher app alias: firmware.bin")
if [[ -f "$FACTORY_SRC" ]]; then
  cp -f -- "$FACTORY_SRC" "$DL/firmware.factory.bin"
  chmod 644 "$DL/firmware.factory.bin"
  PUBLISHED+=("Browser flasher factory alias: firmware.factory.bin")
fi

cp -f -- "$INDEX_SRC" "$WWW/index.html"
chmod 644 "$WWW/index.html"

prune_role() {
  local suffix="$1"
  local -a old=()
  mapfile -t old < <(
    find "$DL" -maxdepth 1 -type f -name "tofu-reader-*-${ENV_NAME}-${suffix}" \
      -printf '%T@ %p\n' | sort -rn | tail -n "+$((KEEP + 1))" | cut -d' ' -f2-
  )
  if ((${#old[@]})); then
    rm -f -- "${old[@]}"
  fi
}

prune_role "app.bin"
prune_role "factory.bin"
prune_role "bootloader.bin"
prune_role "partitions.bin"

export TOFU_VERSION="$VERSION"
export TOFU_ENV="$ENV_NAME"
export TOFU_BRANCH="$BRANCH"
export TOFU_COMMIT="$COMMIT"
export TOFU_COMMIT_FULL="$COMMIT_FULL"
export TOFU_DIRTY="$DIRTY"
export TOFU_KEEP="$KEEP"

python3 - "$DL" "$WWW/manifest.json" <<'PY'
import datetime
import fnmatch
import json
import os
import subprocess
import sys
import tempfile

dl_dir, manifest_path = sys.argv[1], sys.argv[2]
version = os.environ["TOFU_VERSION"]
env = os.environ["TOFU_ENV"]
keep = int(os.environ["TOFU_KEEP"])
dirty = os.environ["TOFU_DIRTY"] == "true"

roles = [
    ("app", "App image", "app.bin"),
    ("factory", "Factory image", "factory.bin"),
    ("bootloader", "Bootloader", "bootloader.bin"),
    ("partitions", "Partition table", "partitions.bin"),
]


def sha256(path):
    result = subprocess.run(
        ["sha256sum", "--", path],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.split()[0]


def size_human(size):
    if size >= 1024 * 1024:
        return "%.1f MB" % (size / (1024 * 1024))
    return "%d KB" % (size / 1024)


def published_at(timestamp):
    return datetime.datetime.fromtimestamp(timestamp).astimezone().isoformat(timespec="seconds")


def entry(name):
    path = os.path.join(dl_dir, name)
    size = os.stat(path).st_size
    return {
        "name": name,
        "path": "dl/" + name,
        "bytes": size,
        "sizeHuman": size_human(size),
        "sha256": sha256(path),
    }

files = []
current_app_name = "tofu-reader-%s-%s-app.bin" % (version, env)
for role, label, suffix in roles:
    name = "tofu-reader-%s-%s-%s" % (version, env, suffix)
    if os.path.isfile(os.path.join(dl_dir, name)):
        item = entry(name)
        files.append({
            "role": role,
            "label": label,
            "name": item["name"],
            "path": item["path"],
            "bytes": item["bytes"],
            "sizeHuman": item["sizeHuman"],
            "sha256": item["sha256"],
        })

pattern = "tofu-reader-*-%s-app.bin" % env
candidates = []
for name in os.listdir(dl_dir):
    if name == current_app_name:
        continue
    if not fnmatch.fnmatch(name, pattern):
        continue
    path = os.path.join(dl_dir, name)
    if os.path.isfile(path):
        candidates.append((os.stat(path).st_mtime, name))

candidates.sort(key=lambda item: item[0], reverse=True)
history = []
for timestamp, name in candidates[:keep]:
    item = entry(name)
    history.append({
        "version": name[len("tofu-reader-"):-len("-%s-app.bin" % env)],
        "name": item["name"],
        "path": item["path"],
        "publishedAt": published_at(timestamp),
        "bytes": item["bytes"],
        "sizeHuman": item["sizeHuman"],
        "sha256": item["sha256"],
    })

manifest = {
    "product": "Tofu Reader",
    "generatedAt": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
    "build": {
        "version": version,
        "branch": os.environ["TOFU_BRANCH"],
        "commit": os.environ["TOFU_COMMIT"],
        "commitFull": os.environ["TOFU_COMMIT_FULL"],
        "dirty": dirty,
        "env": env,
    },
    "files": files,
    "history": history,
}

fd, temp_path = tempfile.mkstemp(dir=os.path.dirname(manifest_path), prefix=".manifest.")
try:
    with os.fdopen(fd, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    os.chmod(temp_path, 0o644)
    os.replace(temp_path, manifest_path)
except BaseException:
    if os.path.exists(temp_path):
        os.unlink(temp_path)
    raise
PY

chmod 755 "$WWW" "$DL"
chmod 644 "$WWW/manifest.json"

APP_DEST="$DL/tofu-reader-${VERSION}-${ENV_NAME}-app.bin"
APP_SHA="$(sha256sum -- "$APP_DEST" | cut -d' ' -f1)"

echo "Published Tofu Reader $VERSION ($ENV_NAME)"
for item in "${PUBLISHED[@]}"; do
  echo "  $item"
done
echo "  App image sha256: $APP_SHA"
echo "  Page: http://100.124.226.84:8099/"
