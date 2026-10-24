// Sleep widgets split into two phases. capture() runs once at sleep entry and
// may touch the HAL and IO (clock, battery). draw() runs inside a display flush
// and may not allocate, format text or read files; it only paints.
#include "SleepWidgets.h"

#include <BatteryMonitor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <Logging.h>

namespace sleep_widgets {

void capture(Context& ctx, const int pageWidth, const int pageHeight) {
  ctx.pageWidth = pageWidth;
  ctx.pageHeight = pageHeight;

  ctx.timeValid = halClock.isAvailable() &&
                  halClock.formatTime(ctx.timeText, sizeof(ctx.timeText), SETTINGS.clockFormat == 1);
  if (!ctx.timeValid) LOG_DBG("SLP", "Sleep widget clock unavailable");

  const BatteryMonitor battery;
  ctx.batteryValid = battery.readPercentageChecked(ctx.batteryPercent);
}

void draw(const GfxRenderer& renderer, const CrossPointSettings::SLEEP_WIDGET kind, const Context& ctx) {
  (void)renderer;
  (void)ctx;
  switch (kind) {
    case CrossPointSettings::SLEEP_WIDGET_CLOCK:
      // Phase 2: render ctx.timeText
      return;
    case CrossPointSettings::SLEEP_WIDGET_NONE:
    default:
      return;
  }
}

}  // namespace sleep_widgets
