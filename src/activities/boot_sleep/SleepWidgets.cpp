// Sleep widgets split into two phases. capture() runs once at sleep entry and
// may touch the HAL and IO (clock, battery). draw() runs inside a display flush
// and may not allocate or read files; it only paints with stack buffers.
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

constexpr int kWidgetMargin = 40;
constexpr int MIN_CELL = 12;

// Todo row geometry. capture() runs before the renderer exists, so the
// panel-fit cap below uses the pinned font metrics instead of measuring them:
// NOTOSANS_16 advanceY is 45 px, NOTOSANS_14 advanceY is 40 px.
constexpr int kTodoRowGap = 18;
constexpr int kPinnedNoto16Line = 45;
constexpr int kPinnedNoto14Line = 40;
constexpr int kTodoRowPitch = kPinnedNoto16Line + kTodoRowGap;
constexpr int kTodoHeadingHeight = kPinnedNoto16Line;
constexpr int kTodoHeadingGap = 12;
constexpr int kTodoOverflowHeight = kPinnedNoto14Line;

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

// Top y that centres a content block of contentHeight on the page, never
// rising above the margin.
int centeredTop(const int pageHeight, const int contentHeight) {
  const int top = (pageHeight - contentHeight) / 2;
  return top < kWidgetMargin ? kWidgetMargin : top;
}

// Start of the last UTF-8 codepoint in [0, length), so truncation can drop a
// whole character instead of a continuation byte.
size_t lastCodepointStart(const char* text, const size_t length) {
  size_t i = length;
  while (i > 0) {
    --i;
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) return i;
  }
  return 0;
}

// Copies text into out with a trailing "..." when it is wider than maxWidth.
// Walks back whole UTF-8 codepoints and allocates nothing.
void fitTextToWidth(const GfxRenderer& renderer, const int fontId, const char* text, const int maxWidth, char* out,
                    const size_t outSize) {
  if (outSize == 0) return;

  const int ellipsisWidth = renderer.getTextWidth(fontId, "...");
  if (ellipsisWidth > maxWidth) {
    out[0] = '\0';
    return;
  }

  if (outSize < 4) {
    out[0] = '\0';
    return;
  }
  size_t length = strlen(text);
  if (length >= outSize) length = outSize - 1;
  memcpy(out, text, length);
  out[length] = '\0';
  if (renderer.getTextWidth(fontId, out) <= maxWidth) return;

  // Too wide: reserve room for the trailing ellipsis and its terminator.
  if (length > outSize - 4) {
    length = outSize - 4;
    out[length] = '\0';
  }
  const int budget = maxWidth - ellipsisWidth;
  while (length > 0) {
    length = lastCodepointStart(out, length);
    out[length] = '\0';
    if (renderer.getTextWidth(fontId, out) <= budget) break;
  }

  size_t body = length;
  memcpy(out + body, "...", 3);
  out[body + 3] = '\0';
  // Kerning across the join can add a pixel; drop whole codepoints from the
  // body until the composed string fits.
  while (body > 0 && renderer.getTextWidth(fontId, out) > maxWidth) {
    const size_t next = lastCodepointStart(out, body);
    memmove(out + next, out + body, 3);
    out[next + 3] = '\0';
    body = next;
  }
}

void drawCalendar(const GfxRenderer& renderer, const Context& ctx) {
  if (!ctx.dateValid || ctx.month < 1 || ctx.month > 12) return;

  const int cellW = (ctx.pageWidth - 2 * kWidgetMargin) / 7;
  if (cellW < MIN_CELL) return;

  const int weeks = (ctx.firstColumn + ctx.daysInMonth + 6) / 7;
  if (weeks <= 0) return;

  const int titleLine = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  const int weekdayLine = renderer.getLineHeight(NOTOSANS_12_FONT_ID);
  const int dayLine = renderer.getLineHeight(NOTOSANS_14_FONT_ID);
  // Line height plus a small gap keeps the grid compact instead of stretching
  // the cells over the remaining panel height.
  const int cellH = dayLine + 12;
  if (cellH < MIN_CELL) return;

  const int weekdayOffset = titleLine;
  const int gridOffset = weekdayOffset + weekdayLine;
  const int contentH = gridOffset + weeks * cellH;
  const int top = centeredTop(ctx.pageHeight, contentH);

  char title[16];
  snprintf(title, sizeof(title), "%s %u", I18n::getInstance().get(MONTH_LABELS[ctx.month - 1]),
           static_cast<unsigned>(ctx.year));
  renderer.drawCenteredText(NOTOSANS_18_FONT_ID, top, title, true, EpdFontFamily::BOLD);

  const int weekdayY = top + weekdayOffset;
  for (uint8_t col = 0; col < 7; ++col) {
    const char* label = I18n::getInstance().get(WEEKDAY_LABELS[col]);
    const int x = kWidgetMargin + col * cellW + (cellW - renderer.getTextWidth(NOTOSANS_12_FONT_ID, label)) / 2;
    renderer.drawText(NOTOSANS_12_FONT_ID, x, weekdayY, label, true);
  }

  const int gridTop = top + gridOffset;
  char dayText[4];
  for (uint8_t day = 1; day <= ctx.daysInMonth; ++day) {
    const int slot = ctx.firstColumn + day - 1;
    const int col = slot % 7;
    const int row = slot / 7;
    const int cellX = kWidgetMargin + col * cellW;
    const int cellY = gridTop + row * cellH;
    const bool today = day == ctx.day;

    if (today) renderer.fillRect(cellX + 2, cellY + 2, cellW - 4, cellH - 4, true);

    snprintf(dayText, sizeof(dayText), "%u", static_cast<unsigned>(day));
    const int textX = cellX + (cellW - renderer.getTextWidth(NOTOSANS_14_FONT_ID, dayText)) / 2;
    const int textY = cellY + (cellH - dayLine) / 2;
    renderer.drawText(NOTOSANS_14_FONT_ID, textX, textY, dayText, !today);
  }
}

void drawTodo(const GfxRenderer& renderer, const Context& ctx) {
  if (ctx.todoCount == 0) return;

  const int lineHeight = renderer.getLineHeight(NOTOSANS_16_FONT_ID);
  const int rowPitch = lineHeight + kTodoRowGap;
  const int overflowLine = ctx.todoOverflow > 0 ? renderer.getLineHeight(NOTOSANS_14_FONT_ID) : 0;
  const int contentH = lineHeight + kTodoHeadingGap + ctx.todoCount * rowPitch + overflowLine;
  const int top = centeredTop(ctx.pageHeight, contentH);

  renderer.drawCenteredText(NOTOSANS_16_FONT_ID, top, tr(STR_TODO_LIST), true, EpdFontFamily::BOLD);

  constexpr int kBoxSize = 18;
  constexpr int kBoxGap = 14;
  const int textX = kWidgetMargin + kBoxSize + kBoxGap;
  const int maxTextWidth = ctx.pageWidth - kWidgetMargin - textX;

  int rowY = top + lineHeight + kTodoHeadingGap;
  for (uint8_t i = 0; i < ctx.todoCount; ++i) {
    const int boxY = rowY + (lineHeight - kBoxSize) / 2;
    renderer.drawRect(kWidgetMargin, boxY, kBoxSize, kBoxSize, 2, true);
    if (ctx.todoDone[i]) {
      // An X reads as finished at this size; a strike-through would not.
      constexpr int kInset = 4;
      const int x0 = kWidgetMargin + kInset;
      const int x1 = kWidgetMargin + kBoxSize - kInset - 1;
      const int y0 = boxY + kInset;
      const int y1 = boxY + kBoxSize - kInset - 1;
      renderer.drawLine(x0, y0, x1, y1, true);
      renderer.drawLine(x1, y0, x0, y1, true);
    }

    char line[sizeof(ctx.todoText[0])];
    fitTextToWidth(renderer, NOTOSANS_16_FONT_ID, ctx.todoText[i], maxTextWidth, line, sizeof(line));
    renderer.drawText(NOTOSANS_16_FONT_ID, textX, rowY, line, true);
    rowY += rowPitch;
  }

  if (ctx.todoOverflow > 0) {
    char more[16];
    snprintf(more, sizeof(more), "+%u more", static_cast<unsigned>(ctx.todoOverflow));
    renderer.drawText(NOTOSANS_14_FONT_ID, kWidgetMargin, rowY, more, true);
  }
}

// Rows that fit between the margins. The heading, its gap and one overflow
// line are fixed costs:
//   cap = (pageHeight - 2*40 - 45 - 12 - 40) / 63
// On 480x800 that is (800 - 80 - 97) / 63 = 9, so kMaxWidgetItems (8) wins.
// On a 480-tall panel it is (480 - 80 - 97) / 63 = 4.
uint8_t todoRowCap(const int pageHeight) {
  const int usable = pageHeight - 2 * kWidgetMargin;
  const int fixed = kTodoHeadingHeight + kTodoHeadingGap + kTodoOverflowHeight;
  const int fitRows = (usable - fixed) / kTodoRowPitch;
  if (fitRows <= 0) return 0;
  if (fitRows >= Context::kMaxWidgetItems) return Context::kMaxWidgetItems;
  return static_cast<uint8_t>(fitRows);
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
  const uint8_t cap = todoRowCap(pageHeight);
  // Keep done items: a finished task stays on the sleep screen as a record of
  // the day until the user deletes it in Settings. Stored order is preserved.
  for (const auto& item : TODO_STORE.getItems()) {
    if (ctx.todoCount < cap) {
      snprintf(ctx.todoText[ctx.todoCount], sizeof(ctx.todoText[ctx.todoCount]), "%s", item.text.c_str());
      ctx.todoDone[ctx.todoCount] = item.done;
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
