#pragma once
#include <GfxRenderer.h>

#include "CrossPointSettings.h"

namespace sleep_widgets {
// Everything a widget may need, captured before the draw so the draw path does
// no IO and no allocation. Sleep entry runs on a time budget.
struct Context {
  // Preformatted local time ("HH:MM" or "H:MM AM"/"HH:MM PM"), filled by capture()
  // through halClock so timezone and the user's 12/24h setting are already applied.
  // The draw path only renders this string: no formatting, no IO.
  bool timeValid = false;
  char timeText[10] = {0};
  bool batteryValid = false;
  uint16_t batteryPercent = 0;
  int pageWidth = 0;
  int pageHeight = 0;
};

void capture(Context& ctx, int pageWidth, int pageHeight);
void draw(const GfxRenderer& renderer, CrossPointSettings::SLEEP_WIDGET kind, const Context& ctx);
}  // namespace sleep_widgets
