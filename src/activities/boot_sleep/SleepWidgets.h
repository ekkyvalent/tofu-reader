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

  // Todo widget snapshot: every stored item, done or not, copied out of
  // TodoStore so the draw path never touches std::string or the SD card. A
  // finished item stays on the sleep screen as a record of the day until the
  // user deletes it in Settings. Sized +1 for the terminator.
  static constexpr uint8_t kMaxWidgetItems = 8;
  uint8_t todoCount = 0;
  uint8_t todoOverflow = 0;  // items beyond the panel-fit cap that did not fit
  bool todoDone[kMaxWidgetItems] = {};
  char todoText[kMaxWidgetItems][41] = {};

  // Now Reading / Clips snapshot. Both widgets resolve the same book, so the
  // title and author are shared. capture() copies them out of the book and
  // reads one clipping; draw() only paints these fixed buffers.
  static constexpr uint8_t kBookTitleSize = 72;
  static constexpr uint8_t kBookAuthorSize = 64;
  static constexpr uint16_t kClipQuoteSize = 256;  // 255 bytes plus terminator
  bool bookResolved = false;                      // a book path was found
  bool clipValid = false;                         // an eligible quote was captured
  int8_t bookPercent = -1;                        // 0..100, or -1 when unknown
  char bookTitle[kBookTitleSize] = {};
  char bookAuthor[kBookAuthorSize] = {};
  char clipQuote[kClipQuoteSize] = {};
};

void capture(Context& ctx, int pageWidth, int pageHeight);
void draw(const GfxRenderer& renderer, CrossPointSettings::SLEEP_WIDGET kind, const Context& ctx);
}  // namespace sleep_widgets
