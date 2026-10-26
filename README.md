# Tofu Reader

A fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) for the **Xteink
X4 Pro**, with a short list of additions on top: frontlight presets you can cycle with a double tap,
four sleep screen widgets, and a todo list you can reorder from the shelf.

Tofu Reader 1.2.0 is based on CrossPoint `develop`, upstream commit `9ba67ea5`.

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

The widget is a sleep screen mode of its own, not an overlay on top of the others. Open
**Widgets** on the main menu and pick which widget to show; choosing one also switches the sleep
screen to Widget mode:

| Widget | What it draws |
| --- | --- |
| **Calendar** | The full month, weekday header, today boxed (Monday or Sunday first) |
| **Todo list** | Up to eight rows, a checkbox per row, block centred on the page |
| **Now Reading** | The book you were last in: cover, title, author, progress bar and percentage |
| **Clips** | One saved clipping as a quote card, with the book title and author underneath |

A finished todo is struck through and keeps its checkbox, and by default it stays on the sleep screen
until you delete it, so the page doubles as a record of the day instead of a list that empties itself.
**Show completed tasks** turns that off if you prefer a clean list.

**Now Reading** resolves the book you last had open, falling back to the top of the recent list. The
cover is rendered at widget size in 1-bit through the same converter the home grid uses, so nothing is
resampled during the sleep paint, and it keeps the book's own aspect ratio inside a framed 280x420 box.
The percentage comes from the same progress file the home screen reads.

**Clips** picks one clipping from the current book and sets it as a quote card. The pick rotates once
per calendar day and only considers real passages, at least six words and 80 to 300 bytes, so a
four-word fragment never ends up filling the panel. With no clipping saved yet it shows a placeholder
line instead of an empty card.

Everything else lives in the same **Widgets** menu: **Widget theme** (Light or Dark, where Dark
inverts the page to white on black while the Now Reading cover keeps its normal polarity),
**Show completed tasks**, **Todo rows** (1-8), **Widget text size** (Normal or Large), and
**Week starts on Sunday** for the calendar.

Todos are edited on the device, no computer needed: **Widgets > Todo list**. Add an item, edit its
text, mark it done or not done, move it up, move it down, send it to the top or the bottom, delete it.
The move entries only appear where the row can actually move, so the first item never offers Move up.
Sixteen items maximum, 64 bytes each. They live in `/.crosspoint/todos.json`, and the order you set is
the order the widget draws.

All four widgets use the same layout rules: 40 px margin on all four sides, content block centred
vertically, text sized for the panel rather than scaled down from a desktop face.

## What comes from CrossPoint

Everything else. The EPUB engine (rendering, hyphenation, kerning, CJK ruby, tables, images,
footnotes, dictionary, KOReader sync), the library and file browser, WiFi transfer and the web UI,
OPDS, custom SD card fonts, USB drive mode, screenshots, Bluetooth page turners and the settings
system are upstream's work, unchanged apart from the additions above.

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

1. Download `tofu-reader-v1.2.0-x4pro.bin` from [Releases](../../releases).
2. Copy it anywhere on the SD card.
3. On the device open **Settings > SD Card Firmware Update**, pick the file, confirm.
4. The image is written to the inactive slot and the device reboots into it. The previous image stays
   on the other slot, so a bad build is recoverable without a cable.

### 2. Full flash (fresh device, or one that will not boot)

Download `tofu-reader-v1.2.0-x4pro-factory.bin` and write it at offset `0x0`:

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0 tofu-reader-v1.2.0-x4pro-factory.bin
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
| `/.crosspoint/todos.json` | Todo list for the sleep screen widget, in display order |
| `/.crosspoint/state.json` | Resume and runtime state |

The per-book cache also holds `cover_widget_280x420_bw.bmp` once the Now Reading widget has drawn
that book, so the cover is not re-decoded every time the device sleeps.

Settings written by CrossPoint are read as is. The one migration this fork performs runs once on the
first boot of the preset feature: a saved `ToggleFrontlight` Home button action becomes
`CycleFrontlightPreset`, recorded as `forkSettingsVersion` in the settings file.

## Updates and versioning

`VERSION` at the repo root is the source of truth, and it must agree with the `version =` line in
`platformio.ini`: the release workflow rejects a tag that disagrees with it, so the two are bumped
together. The target is appended to the version, so the firmware reports `1.2.0-x4pro` in
**Settings > About** and in its HTTP user agent.

Bumps follow the rule in [docs/versioning.md](./docs/versioning.md): `x.x.1` for a fix, `x.1.x` for a
new feature, `1.x.x` for a breaking change. `python3 scripts/bump_version.py minor` rewrites both
files, all-or-nothing, and prints the git commands that cut the release.

**Settings > Check for updates** reads the latest release published in this repository, not from
upstream, so a device running Tofu Reader is never pulled onto a CrossPoint build. The updater matches
an asset named `tofu-reader-v<tag>-x4pro.bin`, which is what `scripts/package-release.sh` produces;
a release packaged any other way is invisible to it.

## Following upstream

This fork rebases onto CrossPoint's `develop` instead of merging, which keeps the branch linear and the
local features as a short stack of commits on top. The base commit is stated at the top of this file.
A rebase rewrites history, so force pushes after one are expected.

Device builds never follow upstream automatically: updates only arrive through releases published in
this repository.

## Documentation

- [User Guide](./USER_GUIDE.md)
- [Versioning and releases](./docs/versioning.md)
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
and documentation are upstream's work. This fork tracks upstream `develop`; the last upstream sync
was at commit `9ba67ea5`.

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
