// Sleep widgets split into two phases. capture() runs once at sleep entry and
// may touch the HAL and IO (clock, battery). draw() runs inside a display flush
// and may not allocate, format text or read files; it only paints.
#include "SleepWidgets.h"

#include <BatteryMonitor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "TodoStore.h"
#include "fontIds.h"

namespace sleep_widgets {
namespace {

constexpr int MARGIN = 24;
constexpr int MIN_CELL = 8;

constexpr StrId MONTH_LABELS[12] = {
    StrId::STR_WIDGET_MONTH_JAN, StrId::STR_WIDGET_MONTH_FEB, StrId::STR_WIDGET_MONTH_MAR,
    StrId::STR_WIDGET_MONTH_APR, StrId::STR_WIDGET_MONTH_MAY, StrId::STR_WIDGET_MONTH_JUN,
    StrId::STR_WIDGET_MONTH_JUL, StrId::STR_WIDGET_MONTH_AUG, StrId::STR_WIDGET_MONTH_SEP,
    StrId::STR_WIDGET_MONTH_OCT, StrId::STR_WIDGET_MONTH_NOV, StrId::STR_WIDGET_MONTH_DEC,
};

// Monday-first.
constexpr StrId WEEKDAY_LABELS[7] = {
    StrId::STR_WIDGET_DOW_MON, StrId::STR_WIDGET_DOW_TUE, StrId::STR_WIDGET_DOW_WED, StrId::STR_WIDGET_DOW_THU,
    StrId::STR_WIDGET_DOW_FRI, StrId::STR_WIDGET_DOW_SAT, StrId::STR_WIDGET_DOW_SUN,
};

uint8_t daysInMonth(const uint16_t year, const uint8_t month) {
  static constexpr uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month != 2) return DAYS[month - 1];
  const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  return leap ? 29 : 28;
}

void drawCalendar(const GfxRenderer& renderer, const Context& ctx) {
  if (!ctx.dateValid || ctx.month < 1 || ctx.month > 12) return;

  const int cellW = (ctx.pageWidth - 2 * MARGIN) / 7;
  if (cellW < MIN_CELL) return;

  const int weeks = (ctx.firstColumn + ctx.daysInMonth + 6) / 7;
  if (weeks <= 0) return;

  const int headerY = MARGIN + 6;
  const int weekdayY = headerY + renderer.getLineHeight(UI_10_FONT_ID);
  const int gridTop = weekdayY + renderer.getLineHeight(SMALL_FONT_ID);
  const int cellH = (ctx.pageHeight - MARGIN - gridTop) / weeks;
  if (cellH < MIN_CELL) return;
  // Cells should stay near-square. Letting them eat every remaining pixel turns a
  // five-week month into a tall empty table that reads worse than a smaller grid.
  const int cellHPx = cellH > cellW + 8 ? cellW + 8 : cellH;

  char header[16];
  snprintf(header, sizeof(header), "%s %u", I18n::getInstance().get(MONTH_LABELS[ctx.month - 1]),
           static_cast<unsigned>(ctx.year));
  renderer.drawCenteredText(UI_10_FONT_ID, headerY, header, true, EpdFontFamily::BOLD);

  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  for (uint8_t col = 0; col < 7; ++col) {
    const char* label = I18n::getInstance().get(WEEKDAY_LABELS[col]);
    const int x = MARGIN + col * cellW + (cellW - renderer.getTextWidth(SMALL_FONT_ID, label)) / 2;
    renderer.drawText(SMALL_FONT_ID, x, weekdayY, label, true);
  }

  char dayText[4];
  for (uint8_t day = 1; day <= ctx.daysInMonth; ++day) {
    const int slot = ctx.firstColumn + day - 1;
    const int col = slot % 7;
    const int row = slot / 7;
    const int cellX = MARGIN + col * cellW;
    const int cellY = gridTop + row * cellHPx;
    const bool today = day == ctx.day;

    if (today) renderer.fillRect(cellX + 2, cellY + 2, cellW - 4, cellHPx - 4, true);

    snprintf(dayText, sizeof(dayText), "%u", static_cast<unsigned>(day));
    const int textX = cellX + (cellW - renderer.getTextWidth(SMALL_FONT_ID, dayText)) / 2;
    const int textY = cellY + (cellHPx - lineHeight) / 2;
    renderer.drawText(SMALL_FONT_ID, textX, textY, dayText, !today);
  }
}

void drawTodo(const GfxRenderer& renderer, const Context& ctx) {
  if (ctx.todoCount == 0) return;

  const int headingY = MARGIN + 6;
  renderer.drawCenteredText(UI_10_FONT_ID, headingY, tr(STR_TODO_LIST), true, EpdFontFamily::BOLD);

  const int lineStep = renderer.getLineHeight(SMALL_FONT_ID) + 6;
  const char* prefix = "[ ] ";
  const int prefixWidth = renderer.getTextWidth(SMALL_FONT_ID, prefix);
  const int available = ctx.pageWidth - MARGIN - (MARGIN + prefixWidth);
  int y = headingY + renderer.getLineHeight(UI_10_FONT_ID);

  for (uint8_t i = 0; i < ctx.todoCount; ++i) {
    char line[sizeof(ctx.todoText[0])];
    snprintf(line, sizeof(line), "%s", ctx.todoText[i]);

    size_t len = strlen(line);
    bool truncated = false;
    if (renderer.getTextWidth(SMALL_FONT_ID, line) > available) {
      truncated = true;
      const int textWidth = available - renderer.getTextWidth(SMALL_FONT_ID, "...");
      char dropped = '\0';
      while (len > 0) {
        dropped = line[len];
        line[len] = '\0';
        if (renderer.getTextWidth(SMALL_FONT_ID, line) <= textWidth) break;
        --len;
      }
      // Do not leave a split UTF-8 sequence at the end of the truncated text.
      if ((static_cast<unsigned char>(dropped) & 0xC0) == 0x80) {
        while (len > 0 && (static_cast<unsigned char>(line[len - 1]) & 0xC0) == 0x80) --len;
        if (len > 0) --len;
        line[len] = '\0';
      }
    }

    const int textX = MARGIN + prefixWidth;
    renderer.drawText(SMALL_FONT_ID, MARGIN, y, prefix, true);
    renderer.drawText(SMALL_FONT_ID, textX, y, line, true);
    if (truncated) {
      renderer.drawText(SMALL_FONT_ID, textX + renderer.getTextWidth(SMALL_FONT_ID, line), y, "...", true);
    }
    y += lineStep;
  }

  if (ctx.todoOverflow > 0) {
    char more[16];
    snprintf(more, sizeof(more), "+%u more", static_cast<unsigned>(ctx.todoOverflow));
    renderer.drawText(SMALL_FONT_ID, MARGIN, y, more, true);
  }
}

}  // namespace

void capture(Context& ctx, const int pageWidth, const int pageHeight) {
  ctx.pageWidth = pageWidth;
  ctx.pageHeight = pageHeight;

  struct tm local = {};
  if (halClock.isAvailable() && halClock.localTime(local) && local.tm_year + 1900 >= 2024) {
    ctx.year = static_cast<uint16_t>(local.tm_year + 1900);
    ctx.month = static_cast<uint8_t>(local.tm_mon + 1);
    ctx.day = static_cast<uint8_t>(local.tm_mday);
    // tm_wday is 0 = Sunday; the grid is Monday-first.
    ctx.todayColumn = static_cast<uint8_t>((local.tm_wday + 6) % 7);
    ctx.daysInMonth = daysInMonth(ctx.year, ctx.month);
    ctx.firstColumn = static_cast<uint8_t>((ctx.todayColumn + 7 - ((ctx.day - 1) % 7)) % 7);
    ctx.dateValid = true;
  } else {
    LOG_DBG("SLP", "Sleep widget calendar date unavailable");
  }

  const BatteryMonitor battery;
  ctx.batteryValid = battery.readPercentageChecked(ctx.batteryPercent);

  TODO_STORE.loadFromFile();
  ctx.todoCount = 0;
  ctx.todoOverflow = 0;
  for (const auto& item : TODO_STORE.getItems()) {
    if (item.done) continue;
    if (ctx.todoCount < Context::kMaxWidgetItems) {
      snprintf(ctx.todoText[ctx.todoCount], sizeof(ctx.todoText[ctx.todoCount]), "%s", item.text.c_str());
      ++ctx.todoCount;
    } else {
      ++ctx.todoOverflow;
    }
  }
}

void draw(const GfxRenderer& renderer, const CrossPointSettings::SLEEP_WIDGET kind, const Context& ctx) {
  switch (kind) {
    case CrossPointSettings::SLEEP_WIDGET_CALENDAR:
      drawCalendar(renderer, ctx);
      return;
    case CrossPointSettings::SLEEP_WIDGET_TODO:
      drawTodo(renderer, ctx);
      return;
    case CrossPointSettings::SLEEP_WIDGET_NONE:
    case CrossPointSettings::SLEEP_WIDGET_COUNT:
      return;
  }
}

}  // namespace sleep_widgets
