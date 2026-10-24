#pragma once
#include <GfxRenderer.h>

#include "CrossPointSettings.h"

namespace sleep_widgets {
// Everything a widget may need, captured before the draw so the draw path does
// no IO and no allocation. Sleep entry runs on a time budget.
struct Context {
  bool dateValid = false;
  uint16_t year = 0;        // e.g. 2026
  uint8_t month = 0;        // 1-12
  uint8_t day = 0;          // 1-31, today
  uint8_t todayColumn = 0;  // 0 = Monday .. 6 = Sunday
  uint8_t firstColumn = 0;  // weekday column of day 1, same mapping
  uint8_t daysInMonth = 0;
  bool batteryValid = false;
  uint16_t batteryPercent = 0;
  int pageWidth = 0;
  int pageHeight = 0;

  // Todo widget snapshot: only undone items, copied out of TodoStore so the draw
  // path never touches std::string or the SD card. sized +1 for the terminator.
  static constexpr uint8_t kMaxWidgetItems = 5;
  uint8_t todoCount = 0;
  uint8_t todoOverflow = 0;  // undone items beyond kMaxWidgetItems that did not fit
  char todoText[kMaxWidgetItems][41] = {};
};

void capture(Context& ctx, int pageWidth, int pageHeight);
void draw(const GfxRenderer& renderer, CrossPointSettings::SLEEP_WIDGET kind, const Context& ctx);
}  // namespace sleep_widgets
