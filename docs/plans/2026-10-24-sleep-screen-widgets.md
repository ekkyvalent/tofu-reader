# Sleep Screen Widgets Implementation Plan

> **For Hermes:** execute task-by-task through Command Code CLI, one build gate per phase.
> Verify each phase independently (diff, build exit code, artifact strings) before starting the next.

**Goal:** put one small, optional widget (clock first) on the e-ink sleep screen, chosen in Settings, painted once at sleep entry so it costs zero extra refreshes.

**Architecture:** exclusive single widget per sleep screen, not a multi-slot layout. A `SleepWidgetKind` enum plus one draw function per kind in a small registry module. The owning `SleepActivity` captures the data (time, battery) into a context struct before painting and calls the widget draw inside the same frame, before the single buffer flush. No heap, no SD or file IO in the draw path, because sleep entry runs on a time budget.

**Tech stack:** C++17, FreeInk SDK HAL (`Rtc`, `BatteryMonitor`, `HalDisplay`), `GfxRenderer` text drawing, `CrossPointSettings` JSON persistence via `SettingsList` `SettingInfo`.

---

## Why this shape (and what is deliberately excluded)

- **Exclusive, not slots.** One widget at a time keeps the config surface to one enum row, deletes layout maths, placement UI and per-slot `fits()` gating. Adding widget N+1 still costs one function plus one enum entry plus one label, which is the extension test that matters.
- **Sleep screen only.** Home already repaints on every tick; a clock there is a refresh per minute and stale data while asleep. The sleep screen is the natural moment: the device is being put down, one full-frame paint is already happening.
- **No live ticking.** Deep sleep freezes time-based UI, and there is no scheduled wake on a BM8563 board. The widget shows the time at sleep entry and that is it. Do not propose a ticking widget.
- **No windowed partial refresh.** `HalDisplay` exposes full-frame refreshes only. Every widget must be drawable in one pass over the existing frame.

## Current code facts (verified in this tree)

| Fact | Where |
| --- | --- |
| Sleep screen modes enum, 9 values, `SLEEP_SCREEN_MODE_COUNT` last | `src/CrossPointSettings.h:20-30` |
| Sleep orchestration and per-mode render functions | `src/activities/boot_sleep/SleepActivity.cpp` (`onEnter` 587, `renderCustomSleepScreen` 658, `renderDefaultSleepScreen` 705, `renderBitmapSleepScreen` 722, `renderCoverSleepScreen` 912, `renderLastScreenSleepScreen` 989, `renderBlankSleepScreen` 1002) |
| Text drawing API already used on sleep screens | `renderer.drawCenteredText(UI_10_FONT_ID, y, str, true, EpdFontFamily::BOLD)` at `SleepActivity.cpp:711-712` |
| Wall clock | `Rtc::now(Rtc::DateTime&)` and `Rtc::present()` in `freeink-sdk/libs/hardware/Rtc/include/Rtc.h` |
| Battery | `BatteryMonitor::readPercentageChecked(uint16_t&)` in `freeink-sdk/libs/hardware/BatteryMonitor/include/BatteryMonitor.h` |
| Setting rows and categories | `src/SettingsList.cpp` (`emit` calls, `StrId::STR_CAT_DISPLAY`) |
| Settings UI sections | `src/activities/settings/SettingsActivity.cpp` (`SettingsSection::Display`) |
| i18n | `lib/I18n/translations/english.yaml`; `gen_i18n.py` fills missing keys from English, so one yaml edit is enough |

---

## Phase 1: the seam (no visible change)

### Task 1.1: settings field and enum

**Files:** modify `src/CrossPointSettings.h` (near the existing sleep screen fields, around line 236-243)

Add, after the existing `sleepScreenCoverFilter` line:

```cpp
  // Optional widget painted onto the sleep screen at sleep entry. 0 = none.
  // Values are persisted; append new kinds before COUNT, never reorder.
  enum SLEEP_WIDGET : uint8_t {
    SLEEP_WIDGET_NONE = 0,
    SLEEP_WIDGET_CLOCK = 1,
    SLEEP_WIDGET_COUNT
  };
  uint8_t sleepScreenWidget = SLEEP_WIDGET_NONE;
```

**Verify:** `grep -n "SLEEP_WIDGET" src/CrossPointSettings.h` shows the enum and the field.

### Task 1.2: the widget module

**Files:** create `src/activities/boot_sleep/SleepWidgets.h`, `src/activities/boot_sleep/SleepWidgets.cpp`

```cpp
// SleepWidgets.h
#pragma once
#include <GfxRenderer.h>

#include "CrossPointSettings.h"

namespace sleep_widgets {
// Everything a widget may need, captured before the draw so the draw path does
// no IO and no allocation. Sleep entry runs on a time budget.
struct Context {
  bool timeValid = false;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t day = 0;
  uint8_t month = 0;
  uint16_t year = 0;
  bool batteryValid = false;
  uint16_t batteryPercent = 0;
  int pageWidth = 0;
  int pageHeight = 0;
};

void capture(Context& ctx);
void draw(const GfxRenderer& renderer, CrossPointSettings::SLEEP_WIDGET kind, const Context& ctx);
}  // namespace sleep_widgets
```

`capture()` fills time from `Rtc::now()` when `Rtc::present()`, battery from
`BatteryMonitor::readPercentageChecked()`, and the page size from the renderer the activity passes in
(pass `int pageWidth, int pageHeight` as parameters instead of storing them if that is simpler).

`draw()` switches on `kind`; `SLEEP_WIDGET_NONE` returns immediately; `SLEEP_WIDGET_CLOCK` is Task 2.1.

### Task 1.3: setting row and labels

**Files:** modify `src/SettingsList.cpp`, modify `lib/I18n/translations/english.yaml`

Add to `SettingsList.cpp` next to the other sleep screen rows:

```cpp
  static constexpr auto SLEEP_WIDGET_VALUES = [] {
    std::array<StrId, CrossPointSettings::SLEEP_WIDGET_COUNT> values{};
    values[CrossPointSettings::SLEEP_WIDGET_NONE] = StrId::STR_WIDGET_NONE;
    values[CrossPointSettings::SLEEP_WIDGET_CLOCK] = StrId::STR_WIDGET_CLOCK;
    return values;
  }();
  emit(SettingInfo::StaticEnum(StrId::STR_SLEEP_SCREEN_WIDGET, &CrossPointSettings::sleepScreenWidget,
                               SLEEP_WIDGET_VALUES, "sleepScreenWidget", StrId::STR_CAT_DISPLAY));
```

Add to `english.yaml` near the sleep screen keys:

```yaml
STR_SLEEP_SCREEN_WIDGET: "Sleep screen widget"
STR_WIDGET_NONE: "None"
STR_WIDGET_CLOCK: "Clock"
```

**Verify:** build passes; the new row appears under Settings > Display; `sleepScreenWidget` shows up in the web settings JSON (it is exposed automatically through `SettingInfo`).

### Task 1.4: paint hook in the activity

**Files:** modify `src/activities/boot_sleep/SleepActivity.h`, `src/activities/boot_sleep/SleepActivity.cpp`

Add to the header:

```cpp
  void drawSleepWidget() const;
```

Implementation captures the context once and calls `sleep_widgets::draw(...)`. It must be called
from every render path that ends in a frame the user actually sees on sleep entry, immediately
before that path's buffer flush, so the widget lands in the same frame instead of forcing a second
full refresh.

**Discovery step for the implementer:** list every `displayBuffer` call inside `SleepActivity.cpp`
and decide per call site whether the widget belongs there. Expected exclusions: `renderBlankSleepScreen`
and `renderLastScreenSleepScreen` (quick resume keeps the live frame; a widget there would need an
extra refresh). Document the decision in a comment at each call site.

**Verify:** build passes; with `sleepScreenWidget = NONE` the rendered sleep screen is byte-for-byte
the same behaviour as before (no extra refresh, no visual change).

### Task 1.5: build gate and commit

Run `pio run -e x4pro`, confirm exit 0 and that Flash grew by less than ~1 KB for the seam.
Commit as `feat(sleep): add an optional sleep screen widget seam with a settings row`.

---

## Phase 2: the clock widget

### Task 2.1: draw the clock

**Files:** modify `src/activities/boot_sleep/SleepWidgets.cpp`

Rules:
- Skip entirely when `ctx.timeValid` is false. A wrong clock is worse than none.
- Format `HH:MM` with `%02u:%02u` into a fixed `char buf[8]`, no `std::string`.
- Draw with `renderer.drawCenteredText(UI_10_FONT_ID, y, buf, true, EpdFontFamily::BOLD)` where `y`
  sits in the bottom band of the page, above the existing sleeping label area. Choose the
  coordinates so the widget does not collide with the cover art's natural focal point.
- Optional second line with the date is a follow-up, not part of this task.

**Verify:** build exit 0; `strings .pio/build/x4pro/firmware.bin | grep -c "Sleep screen widget"` is 1.

### Task 2.2: manual device check

Flash via SD card update and confirm on the device:

1. Settings > Display > Sleep screen widget > Clock.
2. Put the device to sleep. The clock shows the correct local time.
3. No extra refresh flash after the sleep image appears.
4. Set it back to None; the sleep screen is unchanged from before.

### Task 2.3: publish

`bash scripts/publish-local.sh x4pro`, then confirm the page manifest version changed and the app
sha256 matches `.pio/build/x4pro/firmware.bin`.

---

## Phase 3: calendar widget (month grid)

The clock kind added in Phase 2 is **dropped**: the user's picture of this feature is a calendar and
a todo list, nothing else. Renumber the enum to `NONE = 0`, `CALENDAR = 1`, `TODO = 2`, delete the
clock draw case, and delete `timeText` from `Context` if the calendar does not need a formatted
string (the calendar needs the date parts, not "HH:MM"). Nothing persisted survives from the clock
build, so renumbering is safe.

**Files:** `src/activities/boot_sleep/SleepWidgets.{h,cpp}`, `src/CrossPointSettings.h` (enum and
labels), `src/SettingsList.cpp` (label array), `lib/I18n/translations/english.yaml`.

**Geometry:** full month grid, 7 columns, Monday-first. Header with month name and year, weekday
header row, then 5-6 week rows. Highlight today with an inverted box. Compute `daysInMonth` and the
weekday of day 1 in `capture()` with plain arithmetic (no `timegm`, no allocation). Store
`year`, `month`, `day`, `firstWeekday`, `daysInMonth` in `Context`, all plain integers.

**Rules:** no allocation or formatting in `draw()`; cell coordinates derived from `pageWidth` /
`pageHeight` so it stays sane on other panels; skip entirely when the date is unavailable; keep the
`switch` exhaustive.

## Phase 4: todo store and editor (on-device)

Model on the existing OPDS pattern, which already does exactly this shape:

- `src/OpdsServerStore.h` is a `PersistableStore<OpdsServerStore>` singleton holding a
  `std::vector<OpdsServer>` with `addServer` / `updateServer` / `removeServer`.
- `src/activities/settings/OpdsServerListActivity.cpp` is a `UiListActivity` with an "Add" row,
  selection to edit, delete, and text entry through `KeyboardEntryActivity`.

**Deliverables:**
- `src/TodoStore.{h,cpp}`: singleton `PersistableStore<TodoStore>`, `struct TodoItem { std::string text; bool done; }`,
  `std::vector<TodoItem> items`, `addItem`, `updateItem`, `setDone`, `removeItem`, plus a cap
  (recommend 16 items, 64 chars each) so the file and the widget stay bounded.
- `src/activities/settings/TodoListActivity.{h,cpp}`: list of items (done ones marked visually),
  add via keyboard, toggle done, delete with confirmation, and edit text. Same interaction model as
  the OPDS list editor; do not invent a new navigation style.
- Settings → Display (or a new Todos entry next to it): a row that opens the editor.
- Widget rendering is Phase 5, not here.

## Phase 5: todo widget

**Files:** `src/activities/boot_sleep/SleepWidgets.{h,cpp}`.

`capture()` copies up to N (recommend 5) not-done items into a fixed `char` array inside `Context`
(no `std::string`, no heap in the context). `draw()` renders each line with the checkbox glyph and
truncates with an ellipsis when the text does not fit. Skip the widget when there are no items; never
draw an empty box. If more items exist than fit, draw a trailing "+N more" line.

## Out of scope

- Multi-widget layouts, slots, drag placement, per-widget sizing.
- Widgets on Home or in the reader.
- Scheduled wake to refresh the widget.
- Ticking or animated widgets.
- Calendar events, recurring todos, reminders, notifications.
- Touching `partitions.csv`, the bootloader, or the OTA path.

## Risks

- **Sleep entry time budget.** Any SD or network access in the draw path can push sleep entry past
  its budget and leave the device awake or half-slept. Keep `capture()` to RAM-only reads.
- **One frame only.** A widget drawn after the flush costs a second full refresh (~1.7 s) and visible
  ghosting. That is why Task 1.4 puts the call before the flush.
- **Cover modes.** On `COVER`, `COVER_CUSTOM`, `COVER_TRANSPARENT` and `TRANSPARENT_CUSTOM` the
  background is an image, so text needs to stay legible. If a widget cannot be made legible there,
  gate it to the plain modes (`DARK`, `LIGHT`, `CUSTOM`) and say so in the settings label.
- **Persisted enum growth.** `sleepScreenWidget` is a new key, so upgrades default to `NONE`. Never
  reorder `SLEEP_WIDGET` afterwards; append before `COUNT`.
