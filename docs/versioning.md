# Versioning and releases

Tofu Reader uses semantic versioning. `VERSION` at the repository root holds one
bare `MAJOR.MINOR.PATCH` line, and a release is tagged `vX.Y.Z`. The `v` is part of
the contract, not decoration: the on-device updater builds its download name from
the release tag. `scripts/version.py` is the single source of truth for the version
string, and every other piece below consumes it instead of reading `VERSION`.

## The rule

| Bump | Use for | Example |
| --- | --- | --- |
| `x.x.1` patch | A bug fix that changes no interface and adds no feature | `1.0.0` -> `1.0.1` |
| `x.1.x` minor | A new feature or a new increment, backwards compatible | `1.0.0` -> `1.1.0` |
| `1.x.x` major | A breaking change | `1.0.0` -> `2.0.0` |

Worked examples from the current version, `1.0.0`:

| Current | What changed | Next |
| --- | --- | --- |
| `1.0.0` | Patch: a hyphenation pattern made one line render twice; no setting, file format or API changed | `1.0.1` |
| `1.0.0` | Minor: a new sleep-screen widget with its settings row and translation key; existing settings and caches still load | `1.1.0` |
| `1.0.0` | Major: the `todos.json` shape changed, so a `1.0.0` build cannot read what `2.0.0` writes | `2.0.0` |

## Deciding the bump

- New sleep-screen widget, new setting row, new i18n key, new activity: **minor**.
  These extend the existing surface; nothing that was saved before becomes invalid.
- Appending a value to a persisted enum (allowed, no migration needed): **minor**.
  Older builds ignore the trailing value, newer builds understand it.
- Reordering or removing a persisted enum value, changing the on-disk JSON shape of
  a store, changing the partition layout or the NVS blob: **major**. Existing
  devices can no longer trust what they already saved or flashed.
- Rendering, copy, crash, wrong-value or cache-staleness fix: **patch**.
- Dependency or toolchain bump with no behaviour change: **patch**.

A major bump of firmware that has already shipped needs a note in the release body
about downgrade safety. There is no OTA rollback in this tree: the updater only
moves the boot partition forward (`esp_ota_set_boot_partition`), so returning to
the previous major version means flashing over USB.

## Release procedure

The tree must be clean before the tag: `scripts/version.py` reports a development
build whenever `git status --porcelain` is non-empty, including untracked files.
`package-release.sh` refuses to package anything containing `-dev`.

1. Bump the version (or preview it with `--dry-run`):

   ```sh
   python3 scripts/bump_version.py patch
   ```

   The script rewrites both `VERSION` and the `version = ` line in
   `platformio.ini`. CI rejects the tag when they disagree: the "Validate
   release version" step in `.github/workflows/release.yml` (lines 51-60) strips
   the `v` from the tag and compares the result with the first `version = ` line
   in `platformio.ini`, failing the build on a mismatch.

2. Commit only the two version lines:

   ```sh
   git add VERSION platformio.ini
   git commit -m "chore(release): v1.0.1"
   ```

3. Tag the release commit:

   ```sh
   git tag v1.0.1
   ```

4. Push the branch and the tag:

   ```sh
   git push origin HEAD --tags
   ```

5. Build the environment you are releasing (packaging copies, it does not build):

   ```sh
   pio run -e x4pro
   ```

6. Package the release assets:

   ```sh
   ./scripts/package-release.sh x4pro
   ```

7. Upload `dist/tofu-reader-v1.0.1-x4pro.bin` and
   `dist/tofu-reader-v1.0.1-x4pro-factory.bin` to a draft GitHub release for the
   tag:

   ```sh
   gh release create v1.0.1 --draft --title "Tofu Reader 1.0.1" \
     dist/tofu-reader-v1.0.1-x4pro.bin dist/tofu-reader-v1.0.1-x4pro-factory.bin
   ```

   If the draft already exists, upload with
   `gh release upload v1.0.1 dist/tofu-reader-v1.0.1-*.bin --clobber`.

8. Publish the release:

   ```sh
   gh release edit v1.0.1 --draft=false
   ```

Publishing triggers `.github/workflows/release.yml`. Its "Validate release
version" step reads the first `version = ` line in `platformio.ini` (line 55 of
the workflow, not of `platformio.ini`) and compares it with the tag, so keep that
value equal to `VERSION`; the CI envs take `CROSSPOINT_VERSION` from
`platformio.ini` instead of `version.py`.

## Version pieces

| Piece | Path | What it does |
| --- | --- | --- |
| Stored version | `VERSION` | One line, `MAJOR.MINOR.PATCH`, no `v` prefix. Source of truth. |
| Build string | `scripts/version.py` | Clean tree at the `v<VERSION>` tag prints `1.0.0`; everything else prints `1.0.0-dev+<short sha>` or `1.0.0-dev+dirty+<short sha>`. |
| Firmware define | `scripts/git_branch.py` | PlatformIO pre-build step for the `default` and `sticky` dev envs; runs `version.py` and injects the result as `CROSSPOINT_VERSION`. Release envs set that define in `platformio.ini` instead. |
| Local download page | `scripts/publish-local.sh` | Consumes `version.py` (line 113) and publishes `tofu-reader-<version>-<env>-<role>.bin` plus `manifest.json`. |
| Release assets | `scripts/package-release.sh` | Consumes `version.py` (line 54) and writes `dist/tofu-reader-v<version>-<env>.bin` and `dist/tofu-reader-v<version>-<env>-factory.bin` (lines 80-81). |
| On-device OTA | `src/network/OtaUpdater.cpp:57` | Requests `tofu-reader-<tag_name><boardSuffix>.bin`. A tag `v1.0.0` on an x4pro means `tofu-reader-v1.0.0-x4pro.bin`; X3/X4 use the shared `-x3-x4` image (lines 43-48). |
| CI assets | `.github/workflows/release.yml:116` | Names assets `crosspoint-<version>-<device>.bin`. This prefix does not match OTA; see below. |

## Inherited upstream tags

`git tag --list 'v*'` is not the Tofu Reader version history. This repository
also carries tags inherited from upstream CrossPoint: `v1.5.0` is one of them
(upstream commit `e00f5958`, upstream PR #2880), not a Tofu Reader release.
Because it is an ancestor of the current tree, `git describe --tags` may report
`v1.0.0-1-g<sha>`: describe picks the nearest tag, not the newest one. The next
version always comes from `VERSION` plus the bump rules above, never from
`git describe` or the newest `v*` tag.

## Known gap: CI asset names

`.github/workflows/release.yml:116` copies the build to
`crosspoint-${version}-${{ matrix.device }}.bin`, and the upload step at lines
148-153 expects that same `crosspoint-` prefix. `OtaUpdater::checkForUpdate()`
composes `tofu-reader-<tag_name><boardSuffix>.bin` (`src/network/OtaUpdater.cpp:57`),
so on-device OTA cannot find a CI-built asset. Releases packaged with
`scripts/package-release.sh` are the ones OTA can find. The workflow is left as is;
this document does not change it.

## Do not

- Never hand-edit a tag. Re-tag only by moving the tag deliberately and
  re-pushing; a mismatched tag produces assets OTA will not match.
- Never bump `VERSION` in the same commit as the feature. The release commit
  contains the version line only, so the code under the tag is exactly the code
  that was tested.
- Never publish a release whose assets were not named by `package-release.sh`.
  `crosspoint-*` binaries, or any name without the `v` prefix, are invisible to
  the updater.
- Do not derive the next version from `git describe` or the newest `v*` tag.
  Inherited upstream tags such as `v1.5.0` are not Tofu Reader releases.
