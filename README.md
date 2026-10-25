# Tofu Reader

A fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) for the **Xteink
X4 Pro**, with two things added on top: frontlight presets you can cycle with a double tap, and sleep
screen widgets you actually want to look at.

Tofu Reader 1.0.0 is based on CrossPoint 2.0.0rc, upstream commit `facaa87e`.

> **Xteink X4 Pro only.** These builds target the `x4pro` PlatformIO environment (ESP32-S3). Do not
> flash them on an X3, X4, or Picco. CrossPoint covers those devices, this fork does not.

> This fork is not affiliated with Xteink and not supported by upstream. Bugs in the reader engine
> belong in [upstream's tracker](https://github.com/crosspoint-reader/crosspoint-reader/issues).

## What this fork adds

### Frontlight presets you can cycle

Four slots. Each slot is either off, or a brightness and warmth pair you choose. Double tap the Home
button to step through them: 0, 1, 2, 3, back to 0. The current slot is never stored, it is derived
from the live light state, so the cycle stays correct even if the light is changed another way.

- Set the slots in **Settings > Display > Frontlight presets**.
- The double tap is the default Home button action. Change it in **Settings > Controls > Home button**.

### Sleep screen widgets

The widget is a sleep screen mode of its own, not an overlay on top of the others. Choose
**Settings > Display > Sleep Screen > Widget**, then pick which widget to show:

| Widget | What it draws |
| --- | --- |
| **Calendar** | The full month, Monday first, weekday header, today boxed |
| **Todo list** | Up to eight rows in large type, a checkbox per row, block centred on the page |

A finished todo keeps its X and stays on the sleep screen until you delete it, so the page doubles as
a record of the day instead of a list that empties itself.

Todos are edited on the device, no computer needed: **Settings > Display > Todo list**. Add an item,
edit its text, mark it done or not done, delete it. Sixteen items maximum, 64 bytes each. They live in
`/.crosspoint/todos.json`.

Both widgets use the same layout rules: 40 px margin on all four sides, content block centred
vertically, text sized for the panel rather than scaled down from a desktop face.

## What comes from CrossPoint

Everything else. The EPUB engine (rendering, hyphenation, kerning, CJK ruby, tables, images,
footnotes, dictionary, KOReader sync), the library and file browser, WiFi transfer and the web UI,
OPDS, custom SD card fonts, USB drive mode, screenshots, Bluetooth page turners and the settings
system are upstream's work, unchanged apart from the two features above.

Read [CrossPoint's README](https://github.com/crosspoint-reader/crosspoint-reader#readme) and
[user guide](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/USER_GUIDE.md) for
that side of the device. The docs in this repo are upstream's.

## USB-locked devices

Some Xteink units sold through third-party stores ship with USB flashing locked, and the vendor unlock
tool only accepts official CrossPoint and CrossInk images. If yours is locked, unlock it with an
official CrossPoint image first, then update to Tofu Reader from the SD card as described below.

Do not point the unlock tool at anything built here. A wrong image on a locked unit can leave it
stuck with no recovery path.

## Install

### 1. Update from the SD card (normal path)

1. Download `tofu-reader-v1.0.0-x4pro.bin` from [Releases](../../releases).
2. Copy it anywhere on the SD card.
3. On the device open **Settings > SD Card Firmware Update**, pick the file, confirm.
4. The image is written to the inactive slot and the device reboots into it. The previous image stays
   on the other slot, so a bad build is recoverable without a cable.

### 2. Full flash (fresh device, or one that will not boot)

Download `tofu-reader-v1.0.0-x4pro-factory.bin` and write it at offset `0x0`:

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0 tofu-reader-v1.0.0-x4pro-factory.bin
```

That file is a complete flash image: bootloader at `0x0`, partition table at `0x8000`, application at
`0x10000`. Do not write the plain `.bin` at `0x0`, that one is the application image only.

### 3. Build from source

```bash
git clone https://github.com/ekkyvalent/tofu-reader
cd tofu-reader
git submodule update --init --recursive
pio run -e x4pro                # build
pio run -e x4pro -t upload      # flash over USB
```

Requires PlatformIO with the pioarduino core. The `x4pro` environment pulls the ESP32-S3 toolchain and
the FreeInk SDK submodule, so the first build takes a while.

## Files on the SD card

| Path | Contents |
| --- | --- |
| `/.crosspoint/settings.json` | Device settings, same format as CrossPoint plus the fork's preset and widget keys |
| `/.crosspoint/todos.json` | Todo list for the sleep screen widget |
| `/.crosspoint/state.json` | Resume and runtime state |

Settings written by CrossPoint are read as is. The one migration this fork performs runs once on the
first boot of the preset feature: a saved `ToggleFrontlight` Home button action becomes
`CycleFrontlightPreset`, recorded as `forkSettingsVersion` in the settings file.

## Updates and versioning

`VERSION` at the repo root is the source of truth, and the target is appended to it, so the firmware
reports `1.0.0-x4pro` in **Settings > About** and in its HTTP user agent. The local download page
carries the commit as well: `1.0.0-dev+<short sha>` for a build that is not the release tag, and bare
`1.0.0` at the tag.

**Settings > Check for updates** reads releases from this repository, not from upstream, so a device
running Tofu Reader is never pulled onto a CrossPoint build.

## Following upstream

This fork rebases onto CrossPoint's `develop` instead of merging, which keeps the branch linear and the
local features as a short stack of commits on top. The base commit is stated at the top of this file.
A rebase rewrites history, so force pushes after one are expected.

Device builds never follow upstream automatically: updates only arrive through releases published in
this repository.

## Documentation

- [User Guide](./USER_GUIDE.md)
- [Web server usage](./docs/webserver.md)
- [Project scope](./SCOPE.md)
- [Contributing docs](./docs/contributing/README.md)

## Development

```bash
./bin/clang-format-fix
pio check -e x4pro
pio run -e x4pro
```

Serial logs: `python3 scripts/debugging_monitor.py`.

`/.crosspoint` also holds the per-book cache (layout, covers, progress). Removing it forces a full
regeneration on the next open. See [file formats](./docs/file-formats.md).

## Credits and licenses

This firmware exists because of other people's work. Everything here is MIT unless noted otherwise.

**Upstream.** Tofu Reader is a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader),
Copyright (c) 2025 CrossPoint Reader organization, MIT licensed. The reading engine, UI, device support
and documentation are upstream's work. This fork tracks upstream `develop`; the last rebase was at
commit `facaa87e`.

**FreeInk SDK.** The hardware abstraction layer, display drivers, SD card, network and font stacks come
from [freeink-sdk](https://github.com/Free-Ink/freeink-sdk), Copyright (c) 2026 FreeInk, MIT licensed.
It vendors expat (MIT) and, through FreeInkFont, FreeType (FreeType License) and stb_truetype (public
domain / MIT).

**Fonts.** Noto Sans, Noto Serif, Noto Sans Arabic, Noto Sans Hebrew and OpenDyslexic ship under the SIL
Open Font License 1.1, with the license text beside each family in
`lib/EpdFont/builtinFonts/source/*/OFL.txt`.

**Libraries**, pinned in `platformio.ini`:

| Library | License |
| --- | --- |
| ArduinoJson 7.4.2 | MIT |
| SdFat 2.3.1 | MIT |
| QRCode 0.0.1 | MIT |
| PNGdec 1.1.6 | Apache-2.0 |
| JPEGDEC | Apache-2.0 |
| AnimatedGIF | Apache-2.0 |
| NimBLE-Arduino | Apache-2.0 |
| arduinoWebSockets 2.7.3 | LGPL-2.1 |
| Arduino-wolfSSL (wolfSSL 5.7.2) | GPL-2.0, or commercial from wolfSSL |

wolfSSL is the one copyleft dependency that matters when redistributing: the full source ships in this
tree, and a binary built from it carries GPL-2.0 obligations.

**Fork changes.** Frontlight preset cycling, the sleep screen widgets, the todo store and the rebrand
are Copyright (c) 2026 Ekky Pramana, released under the same MIT terms. See `NOTICE` for the fork's
disclaimers. `LICENSE` is upstream's and is unchanged.

## Issues

Issues are open. Bugs in the two features above, or in the `x4pro` build, belong here. Reader engine
bugs that also reproduce on upstream builds belong in
[upstream's tracker](https://github.com/crosspoint-reader/crosspoint-reader/issues).

---

Tofu Reader is **not affiliated with Xteink or any device manufacturer**.
