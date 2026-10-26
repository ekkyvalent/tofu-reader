// Sleep widgets split into two phases. capture() runs once at sleep entry and
// may touch the HAL and IO (clock, battery). draw() runs inside a display flush
// and may not allocate or read files; it only paints with stack buffers.
#include "SleepWidgets.h"

#include <BatteryMonitor.h>
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

#include "ClippingStore.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "TodoStore.h"
#include "fontIds.h"
#include "util/BookProgress.h"

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
    StrId::STR_WIDGET_MONTH_JAN, StrId::STR_WIDGET_MONTH_FEB, StrId::STR_WIDGET_MONTH_MAR, StrId::STR_WIDGET_MONTH_APR,
    StrId::STR_WIDGET_MONTH_MAY, StrId::STR_WIDGET_MONTH_JUN, StrId::STR_WIDGET_MONTH_JUL, StrId::STR_WIDGET_MONTH_AUG,
    StrId::STR_WIDGET_MONTH_SEP, StrId::STR_WIDGET_MONTH_OCT, StrId::STR_WIDGET_MONTH_NOV, StrId::STR_WIDGET_MONTH_DEC,
};

// Monday-first.
constexpr StrId WEEKDAY_LABELS[7] = {
    StrId::STR_WIDGET_DOW_MON, StrId::STR_WIDGET_DOW_TUE, StrId::STR_WIDGET_DOW_WED, StrId::STR_WIDGET_DOW_THU,
    StrId::STR_WIDGET_DOW_FRI, StrId::STR_WIDGET_DOW_SAT, StrId::STR_WIDGET_DOW_SUN,
};

// Widget body text size. Large keeps the shipped look.
bool widgetLargeText() { return SETTINGS.widgetTextSize == CrossPointSettings::WIDGET_TEXT_LARGE; }
int widgetBodyFont(const int largeId, const int normalId) { return widgetLargeText() ? largeId : normalId; }

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

// ---- Now Reading and Clips ------------------------------------------------

// 2:3 box: big enough to read as the focal element, small enough that the whole
// block (label, cover, title, author, bar, percent) still fits 720 usable px.
constexpr int kWidgetCoverBoxWidth = 280;
constexpr int kWidgetCoverBoxHeight = 420;
constexpr int kProgressBarHeight = 10;
constexpr int kProgressBarInset = 2;
constexpr int kGapAfterLabel = 18;
constexpr int kGapAfterCover = 18;
constexpr int kGapTitleAuthor = 6;
constexpr int kGapBeforeBar = 26;
constexpr int kGapBeforePercent = 10;

constexpr int kClipCardPaddingX = 24;
constexpr int kClipCardPaddingY = 26;
constexpr int kClipGlyphTop = 20;
constexpr int kClipGlyphGap = 8;
constexpr int kClipAttributionGap = 18;
constexpr uint8_t kQuoteMaxLines = 10;
constexpr uint8_t kEmptyTextMaxLines = 3;
constexpr size_t kLineScratchSize = 128;

// One wrapped line as an offset/length slice of a captured text buffer.
struct LineSlice {
  uint16_t offset = 0;
  uint16_t length = 0;
};

// The cover Bitmap has no default constructor and must survive from capture()
// into the paint, so it and its open file live here for the whole sleep entry.
HalFile gCoverFile;
std::unique_ptr<Bitmap> gCoverBitmap;

void releaseCover() {
  gCoverBitmap.reset();
  if (gCoverFile) gCoverFile.close();
}

void openCoverBitmap(const char* path) {
  if (path == nullptr || path[0] == '\0') return;
  if (!Storage.openFileForRead("SLP", path, gCoverFile)) return;
  auto bitmap = makeUniqueNoThrow<Bitmap>(gCoverFile);
  if (bitmap && bitmap->parseHeaders() == BmpReaderError::Ok) {
    gCoverBitmap = std::move(bitmap);
  } else {
    gCoverFile.close();
  }
}

// Copies at most outSize-1 bytes of text into out, collapsing every run of
// whitespace to one space and cutting on a UTF-8 codepoint boundary.
void copyCollapsed(char* out, const size_t outSize, const char* text, const size_t textLength) {
  if (outSize == 0) return;
  size_t written = 0;
  bool pendingSpace = false;
  for (size_t i = 0; i < textLength; ++i) {
    const auto c = static_cast<unsigned char>(text[i]);
    const bool whitespace = c == ' ' || c == '\n' || c == '\r' || c == '\t';
    if (whitespace) {
      if (written > 0) pendingSpace = true;
      continue;
    }
    if (pendingSpace) {
      if (written + 1 >= outSize) break;
      out[written++] = ' ';
      pendingSpace = false;
    }
    if (written + 1 >= outSize) break;
    out[written++] = static_cast<char>(c);
  }

  // Drop a trailing partial codepoint left by the length cap.
  size_t start = written;
  while (start > 0 && (static_cast<unsigned char>(out[start - 1]) & 0xC0) == 0x80) --start;
  if (start > 0) {
    const auto lead = static_cast<unsigned char>(out[start - 1]);
    size_t expected = 1;
    if ((lead & 0xE0) == 0xC0)
      expected = 2;
    else if ((lead & 0xF0) == 0xE0)
      expected = 3;
    else if ((lead & 0xF8) == 0xF0)
      expected = 4;
    if (written - (start - 1) < expected) written = start - 1;
  }
  while (written > 0 && out[written - 1] == ' ') --written;
  out[written] = '\0';
}

// Display-only cleanup: repeated metadata values are joined with separators
// (ContentOpfParser joins dc:creator with ", "), leaving junk such as
// "Cal Newport;". Strips trailing whitespace and separator runes in place.
void trimTrailingSeparators(char* text) {
  size_t length = strlen(text);
  while (length > 0) {
    const char c = text[length - 1];
    if (c != ' ' && c != ';' && c != ',' && c != ':' && c != '|' && c != '/') break;
    --length;
  }
  text[length] = '\0';
}

// Greedy word wrap into caller-provided slices. Candidate lines are measured
// in scratch with getTextWidth, so this allocates nothing and stops at the
// caller's line cap.
uint8_t wrapLines(const GfxRenderer& renderer, const int fontId, const char* text, const int maxWidth, LineSlice* lines,
                  const uint8_t maxLines, char* scratch, const size_t scratchSize) {
  if (text == nullptr || lines == nullptr || scratch == nullptr || scratchSize < 2 || maxLines == 0) return 0;
  const size_t length = strlen(text);
  size_t pos = 0;
  uint8_t count = 0;
  while (pos < length && count < maxLines) {
    while (pos < length && text[pos] == ' ') ++pos;
    if (pos >= length) break;

    size_t lineEnd = pos;
    size_t cursor = pos;
    bool anyWord = false;
    while (cursor < length) {
      size_t wordEnd = cursor;
      while (wordEnd < length && text[wordEnd] != ' ') ++wordEnd;
      const size_t candidateLength = wordEnd - pos;
      const size_t copyLength = candidateLength < scratchSize - 1 ? candidateLength : scratchSize - 1;
      memcpy(scratch, text + pos, copyLength);
      scratch[copyLength] = '\0';
      if (candidateLength >= scratchSize || renderer.getTextWidth(fontId, scratch) > maxWidth) break;
      lineEnd = wordEnd;
      anyWord = true;
      cursor = wordEnd;
      while (cursor < length && text[cursor] == ' ') ++cursor;
    }
    if (!anyWord) {
      // The first word alone is wider than the line: emit it and let
      // fitTextToWidth ellipsize it at paint time.
      while (lineEnd < length && text[lineEnd] != ' ') ++lineEnd;
    }
    size_t sliceEnd = lineEnd;
    while (sliceEnd > pos && text[sliceEnd - 1] == ' ') --sliceEnd;
    lines[count].offset = static_cast<uint16_t>(pos);
    lines[count].length = static_cast<uint16_t>(sliceEnd - pos);
    ++count;
    pos = lineEnd;
  }
  return count;
}

// Paints wrapped slices. Each line is copied into a scratch buffer and run
// through fitTextToWidth, so an over-long line ends in an ellipsis instead of
// being clipped by the panel.
void drawWrappedLines(const GfxRenderer& renderer, const int fontId, const char* text, const LineSlice* lines,
                      const uint8_t lineCount, const int x, const int y, const int lineHeight, const int maxWidth,
                      const bool centered) {
  char raw[kLineScratchSize];
  char fitted[kLineScratchSize];
  int lineY = y;
  for (uint8_t i = 0; i < lineCount; ++i) {
    size_t length = lines[i].length;
    if (length >= sizeof(raw)) length = sizeof(raw) - 1;
    memcpy(raw, text + lines[i].offset, length);
    raw[length] = '\0';
    fitTextToWidth(renderer, fontId, raw, maxWidth, fitted, sizeof(fitted));
    int textX = x;
    if (centered) {
      const int width = renderer.getTextWidth(fontId, fitted);
      textX = x + (maxWidth - width) / 2;
    }
    renderer.drawText(fontId, textX, lineY, fitted, true);
    lineY += lineHeight;
  }
}

void drawNowReading(const GfxRenderer& renderer, const Context& ctx) {
  const int labelLine = renderer.getLineHeight(NOTOSANS_12_FONT_ID);
  const int titleFont = widgetBodyFont(NOTOSANS_14_FONT_ID, NOTOSANS_12_FONT_ID);
  const int titleLine = renderer.getLineHeight(titleFont);
  const int authorLine = renderer.getLineHeight(NOTOSANS_12_FONT_ID);
  const int percentLine = renderer.getLineHeight(NOTOSANS_12_FONT_ID);
  const int blockWidth = ctx.pageWidth - 2 * kWidgetMargin;

  if (!ctx.bookResolved) {
    const char* empty = tr(STR_WIDGET_NOW_READING_EMPTY);
    LineSlice lines[kEmptyTextMaxLines];
    char scratch[kLineScratchSize];
    const uint8_t lineCount =
        wrapLines(renderer, titleFont, empty, blockWidth, lines, kEmptyTextMaxLines, scratch, sizeof(scratch));
    const int textLine = titleLine;
    const int contentH = labelLine + kGapAfterLabel + lineCount * textLine;
    const int top = centeredTop(ctx.pageHeight, contentH);
    renderer.drawCenteredText(NOTOSANS_12_FONT_ID, top, tr(STR_WIDGET_NOW_READING));
    drawWrappedLines(renderer, titleFont, empty, lines, lineCount, kWidgetMargin, top + labelLine + kGapAfterLabel,
                     textLine, blockWidth, true);
    releaseCover();
    return;
  }

  int contentH = labelLine + kGapAfterLabel + titleLine + kGapTitleAuthor + authorLine;
  if (gCoverBitmap) contentH += kWidgetCoverBoxHeight + kGapAfterCover;
  if (ctx.bookPercent >= 0) contentH += kGapBeforeBar + kProgressBarHeight + kGapBeforePercent + percentLine;
  const int top = centeredTop(ctx.pageHeight, contentH);

  renderer.drawCenteredText(NOTOSANS_12_FONT_ID, top, tr(STR_WIDGET_NOW_READING));
  int y = top + labelLine + kGapAfterLabel;

  if (gCoverBitmap) {
    const int boxX = (ctx.pageWidth - kWidgetCoverBoxWidth) / 2;
    // A widget-sized bitmap matches the box and paints 1:1. A fallback
    // screen-sized bitmap is shrunk to fit, so centre its shorter height inside
    // the reserved box instead of cropping it.
    const int bitmapWidth = gCoverBitmap->getWidth();
    const int bitmapHeight = gCoverBitmap->getHeight();
    int drawnHeight = kWidgetCoverBoxHeight;
    if (bitmapWidth > 0) {
      const int scaledHeight = bitmapHeight * kWidgetCoverBoxWidth / bitmapWidth;
      if (scaledHeight < drawnHeight) drawnHeight = scaledHeight;
    }
    int drawnWidth = kWidgetCoverBoxWidth;
    if (bitmapHeight > 0) {
      const int scaledWidth = bitmapWidth * kWidgetCoverBoxHeight / bitmapHeight;
      if (scaledWidth < drawnWidth) drawnWidth = scaledWidth;
    }
    // The only IO in the draw path: reading the already-open cover Bitmap
    // while painting. It is the same cost the cover sleep screen already pays
    // in this same activity at the same refresh.
    // drawBitmap anchors at x and never centres, so the drawn origin is computed here.
    const int drawnLeft = boxX + (kWidgetCoverBoxWidth - drawnWidth) / 2;
    const int drawnTop = y + (kWidgetCoverBoxHeight - drawnHeight) / 2;
    renderer.drawBitmap(*gCoverBitmap, drawnLeft, drawnTop, kWidgetCoverBoxWidth, kWidgetCoverBoxHeight, 0, 0, false);
    constexpr int kCoverFrameGap = 6;    // matches COVER_CELL_INSET in the home grid
    constexpr int kCoverFrameWidth = 2;  // matches the Clips card border in this widget set
    renderer.drawRect(drawnLeft - kCoverFrameGap, drawnTop - kCoverFrameGap, drawnWidth + 2 * kCoverFrameGap,
                      drawnHeight + 2 * kCoverFrameGap, kCoverFrameWidth, true);
    y += kWidgetCoverBoxHeight + kGapAfterCover;
  }

  char fitted[kLineScratchSize];
  fitTextToWidth(renderer, titleFont, ctx.bookTitle, blockWidth, fitted, sizeof(fitted));
  renderer.drawCenteredText(titleFont, y, fitted, true, EpdFontFamily::BOLD);
  y += titleLine + kGapTitleAuthor;

  fitTextToWidth(renderer, NOTOSANS_12_FONT_ID, ctx.bookAuthor, blockWidth, fitted, sizeof(fitted));
  renderer.drawCenteredText(NOTOSANS_12_FONT_ID, y, fitted);
  y += authorLine;

  if (ctx.bookPercent >= 0) {
    y += kGapBeforeBar;
    const int barX = kWidgetMargin;
    const int barWidth = ctx.pageWidth - 2 * kWidgetMargin;
    renderer.drawRect(barX, y, barWidth, kProgressBarHeight, 2, true);
    const int filled = ctx.bookPercent * (barWidth - 2 * kProgressBarInset) / 100;
    if (filled > 0) {
      renderer.fillRect(barX + kProgressBarInset, y + kProgressBarInset, filled,
                        kProgressBarHeight - 2 * kProgressBarInset, true);
    }
    y += kProgressBarHeight + kGapBeforePercent;
    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%u%%", static_cast<unsigned>(ctx.bookPercent));
    renderer.drawCenteredText(NOTOSANS_12_FONT_ID, y, percentText);
  }

  releaseCover();
}

void drawClips(const GfxRenderer& renderer, const Context& ctx) {
  const int cardX = kWidgetMargin;
  const int cardWidth = ctx.pageWidth - 2 * kWidgetMargin;
  const int textWidth = cardWidth - 2 * kClipCardPaddingX;
  const int quoteFont = widgetBodyFont(NOTOSANS_14_FONT_ID, NOTOSANS_12_FONT_ID);
  const int quoteLine = renderer.getLineHeight(quoteFont);

  LineSlice lines[kQuoteMaxLines];
  char scratch[kLineScratchSize];
  uint8_t lineCount = 0;
  if (ctx.clipValid) {
    lineCount =
        wrapLines(renderer, quoteFont, ctx.clipQuote, textWidth, lines, kQuoteMaxLines, scratch, sizeof(scratch));
  }

  if (!ctx.clipValid || lineCount == 0) {
    const char* empty = tr(STR_WIDGET_CLIPS_EMPTY);
    lineCount = wrapLines(renderer, quoteFont, empty, textWidth, lines, kEmptyTextMaxLines, scratch, sizeof(scratch));
    const int cardHeight = 2 * kClipCardPaddingY + 3 * quoteLine;
    const int top = centeredTop(ctx.pageHeight, cardHeight);
    renderer.drawRect(cardX, top, cardWidth, cardHeight, 2, true);
    drawWrappedLines(renderer, quoteFont, empty, lines, lineCount, cardX + kClipCardPaddingX, top + kClipCardPaddingY,
                     quoteLine, textWidth, true);
    return;
  }

  const int glyphLine = renderer.getLineHeight(NOTOSANS_16_FONT_ID);
  const int cardHeight = 2 * kClipCardPaddingY + glyphLine + kClipGlyphGap + lineCount * quoteLine;
  const int top = centeredTop(ctx.pageHeight, cardHeight);
  renderer.drawRect(cardX, top, cardWidth, cardHeight, 2, true);
  renderer.drawText(NOTOSANS_16_FONT_ID, cardX + kClipCardPaddingX, top + kClipGlyphTop, "\xe2\x80\x9c", true,
                    EpdFontFamily::BOLD);

  drawWrappedLines(renderer, quoteFont, ctx.clipQuote, lines, lineCount, cardX + kClipCardPaddingX,
                   top + kClipCardPaddingY + glyphLine + kClipGlyphGap, quoteLine, textWidth, false);

  const int attributionTop = top + cardHeight + kClipAttributionGap;
  char fitted[kLineScratchSize];
  fitTextToWidth(renderer, NOTOSANS_12_FONT_ID, ctx.bookTitle, cardWidth - 8, fitted, sizeof(fitted));
  int textW = renderer.getTextWidth(NOTOSANS_12_FONT_ID, fitted, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSANS_12_FONT_ID, cardX + cardWidth - textW, attributionTop, fitted, true, EpdFontFamily::BOLD);
  fitTextToWidth(renderer, NOTOSANS_12_FONT_ID, ctx.bookAuthor, cardWidth - 8, fitted, sizeof(fitted));
  textW = renderer.getTextWidth(NOTOSANS_12_FONT_ID, fitted);
  renderer.drawText(NOTOSANS_12_FONT_ID, cardX + cardWidth - textW,
                    attributionTop + renderer.getLineHeight(NOTOSANS_12_FONT_ID), fitted);
}

// ---- captured book-backed data --------------------------------------------

const char* resolveBookPath() {
  if (!APP_STATE.openEpubPath.empty()) return APP_STATE.openEpubPath.c_str();
  const auto& books = RECENT_BOOKS.getBooks();
  if (books.empty() || books.front().path.empty()) return nullptr;
  return books.front().path.c_str();
}

void captureBookText(const char* path, Context& ctx) {
  // The recent-books index already holds the title and author, so a matching
  // entry avoids opening the book at all.
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (book.path != path) continue;
    copyCollapsed(ctx.bookTitle, sizeof(ctx.bookTitle), book.title.c_str(), book.title.size());
    copyCollapsed(ctx.bookAuthor, sizeof(ctx.bookAuthor), book.author.c_str(), book.author.size());
    if (ctx.bookTitle[0] != '\0') return;
  }

  if (FsHelpers::hasXtcExtension(path)) {
    Xtc book(path, "/.crosspoint");
    if (!book.load()) return;
    auto title = book.getTitle();
    auto author = book.getAuthor();
    copyCollapsed(ctx.bookTitle, sizeof(ctx.bookTitle), title.c_str(), title.size());
    copyCollapsed(ctx.bookAuthor, sizeof(ctx.bookAuthor), author.c_str(), author.size());
  } else if (FsHelpers::hasReflowableBookExtension(path)) {
    Epub book(path, "/.crosspoint");
    if (!book.load(true, true)) return;
    const auto& title = book.getTitle();
    const auto& author = book.getAuthor();
    copyCollapsed(ctx.bookTitle, sizeof(ctx.bookTitle), title.c_str(), title.size());
    copyCollapsed(ctx.bookAuthor, sizeof(ctx.bookAuthor), author.c_str(), author.size());
  }
}

void captureNowReading(const char* path, Context& ctx) {
  if (path == nullptr) return;
  ctx.bookResolved = true;

  if (FsHelpers::hasXtcExtension(path)) {
    Xtc book(path, "/.crosspoint");
    if (book.load()) {
      auto title = book.getTitle();
      auto author = book.getAuthor();
      copyCollapsed(ctx.bookTitle, sizeof(ctx.bookTitle), title.c_str(), title.size());
      copyCollapsed(ctx.bookAuthor, sizeof(ctx.bookAuthor), author.c_str(), author.size());
      if (book.generateCoverBmp()) openCoverBitmap(book.getCoverBmpPath().c_str());
    }
  } else if (FsHelpers::hasReflowableBookExtension(path)) {
    Epub book(path, "/.crosspoint");
    if (book.load(true, true)) {
      const auto& title = book.getTitle();
      const auto& author = book.getAuthor();
      copyCollapsed(ctx.bookTitle, sizeof(ctx.bookTitle), title.c_str(), title.size());
      copyCollapsed(ctx.bookAuthor, sizeof(ctx.bookAuthor), author.c_str(), author.size());
      if (book.generateWidgetCoverBmp(kWidgetCoverBoxWidth, kWidgetCoverBoxHeight)) {
        openCoverBitmap(book.getWidgetCoverBmpPath(kWidgetCoverBoxWidth, kWidgetCoverBoxHeight).c_str());
      } else if (book.generateCoverBmp()) {
        openCoverBitmap(book.getCoverBmpPath().c_str());
      }
    }
  }

  if (ctx.bookTitle[0] == '\0') {
    // A load failure must still leave readable text; the file name is a
    // reasonable last resort and costs no allocation.
    const char* name = strrchr(path, '/');
    name = name != nullptr ? name + 1 : path;
    const char* dot = strrchr(name, '.');
    const size_t nameLength = dot != nullptr && dot > name ? static_cast<size_t>(dot - name) : strlen(name);
    copyCollapsed(ctx.bookTitle, sizeof(ctx.bookTitle), name, nameLength);
  }

  const int progress = loadBookProgress(path);
  if (progress >= 0 && progress <= 100) ctx.bookPercent = static_cast<int8_t>(progress);
}

bool isEligibleClipping(const Clipping* clipping) {
  if (clipping == nullptr) return false;
  return clipping->wordCount >= 6 && clipping->textLength >= 80 && clipping->textLength <= 300;
}

uint32_t fnv1a(const char* text) {
  uint32_t hash = 2166136261u;
  while (text != nullptr && *text != '\0') {
    hash ^= static_cast<uint8_t>(*text++);
    hash *= 16777619u;
  }
  return hash;
}

uint32_t avalanche(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

void captureClips(Context& ctx) {
  const char* path = resolveBookPath();
  if (path == nullptr) return;
  ctx.bookResolved = true;

  captureBookText(path, ctx);

  if (!CLIPPINGS.loadForBook(path, ctx.bookTitle, ctx.bookAuthor, "epub")) {
    CLIPPINGS.unload();
    return;
  }

  const size_t count = CLIPPINGS.clippingCount();
  size_t eligible = 0;
  for (size_t i = 0; i < count; ++i) {
    if (isEligibleClipping(CLIPPINGS.clippingAt(i))) ++eligible;
  }
  if (eligible == 0) {
    CLIPPINGS.unload();
    return;
  }

  uint32_t day = 0;
  struct tm local = {};
  if (halClock.isAvailable() && halClock.localTime(local)) {
    local.tm_isdst = -1;
    const time_t seconds = mktime(&local);
    if (seconds > 0) day = static_cast<uint32_t>(seconds) / 86400u;
  }

  size_t want = avalanche(fnv1a(path) ^ day) % eligible;
  size_t chosen = count;
  for (size_t i = 0; i < count; ++i) {
    if (!isEligibleClipping(CLIPPINGS.clippingAt(i))) continue;
    if (want == 0) {
      chosen = i;
      break;
    }
    --want;
  }

  if (chosen < count) {
    std::string text;
    if (CLIPPINGS.readClippingText(chosen, text)) {
      copyCollapsed(ctx.clipQuote, sizeof(ctx.clipQuote), text.c_str(), text.size());
      ctx.clipValid = ctx.clipQuote[0] != '\0';
    }
  }
  CLIPPINGS.unload();
}

void drawCalendar(const GfxRenderer& renderer, const Context& ctx) {
  if (!ctx.dateValid || ctx.month < 1 || ctx.month > 12) return;

  const int cellW = (ctx.pageWidth - 2 * kWidgetMargin) / 7;
  if (cellW < MIN_CELL) return;

  const int weeks = (ctx.firstColumn + ctx.daysInMonth + 6) / 7;
  if (weeks <= 0) return;

  const int titleLine = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  const int weekdayLine = renderer.getLineHeight(NOTOSANS_12_FONT_ID);
  const int dayFont = widgetBodyFont(NOTOSANS_14_FONT_ID, NOTOSANS_12_FONT_ID);
  const int dayLine = renderer.getLineHeight(dayFont);
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
  // WEEKDAY_LABELS is Monday-first; shift by six for a Sunday-first week.
  const uint8_t labelShift = ctx.calendarSundayFirst ? 6 : 0;
  for (uint8_t col = 0; col < 7; ++col) {
    const char* label = I18n::getInstance().get(WEEKDAY_LABELS[(col + labelShift) % 7]);
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
    const int textX = cellX + (cellW - renderer.getTextWidth(dayFont, dayText)) / 2;
    const int textY = cellY + (cellH - dayLine) / 2;
    renderer.drawText(dayFont, textX, textY, dayText, !today);
  }
}

void drawTodo(const GfxRenderer& renderer, const Context& ctx) {
  if (ctx.todoCount == 0) return;

  const int rowFont = widgetBodyFont(NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID);
  const int headingLine = renderer.getLineHeight(NOTOSANS_16_FONT_ID);
  const int lineHeight = renderer.getLineHeight(rowFont);
  const int rowPitch = lineHeight + kTodoRowGap;
  const int overflowLine = ctx.todoOverflow > 0 ? renderer.getLineHeight(NOTOSANS_14_FONT_ID) : 0;
  const int contentH = headingLine + kTodoHeadingGap + ctx.todoCount * rowPitch + overflowLine;
  const int top = centeredTop(ctx.pageHeight, contentH);

  renderer.drawCenteredText(NOTOSANS_16_FONT_ID, top, tr(STR_TODO_LIST), true, EpdFontFamily::BOLD);

  constexpr int kBoxSize = 18;
  constexpr int kBoxGap = 14;
  const int textX = kWidgetMargin + kBoxSize + kBoxGap;
  const int maxTextWidth = ctx.pageWidth - kWidgetMargin - textX;

  int rowY = top + headingLine + kTodoHeadingGap;
  for (uint8_t i = 0; i < ctx.todoCount; ++i) {
    const int boxY = rowY + (lineHeight - kBoxSize) / 2;
    renderer.drawRect(kWidgetMargin, boxY, kBoxSize, kBoxSize, 2, true);
    if (ctx.todoDone[i]) {
      // The checkbox keeps its X; the row text is also struck through below.
      constexpr int kInset = 4;
      const int x0 = kWidgetMargin + kInset;
      const int x1 = kWidgetMargin + kBoxSize - kInset - 1;
      const int y0 = boxY + kInset;
      const int y1 = boxY + kBoxSize - kInset - 1;
      renderer.drawLine(x0, y0, x1, y1, true);
      renderer.drawLine(x1, y0, x0, y1, true);
    }

    char line[sizeof(ctx.todoText[0])];
    fitTextToWidth(renderer, rowFont, ctx.todoText[i], maxTextWidth, line, sizeof(line));
    renderer.drawText(rowFont, textX, rowY, line, true);
    if (ctx.todoDone[i]) {
      // Strike the finished text through its middle so "done" reads at a glance.
      const int strikeWidth = renderer.getTextWidth(rowFont, line);
      renderer.fillRect(textX, rowY + lineHeight / 2, strikeWidth, 2, true);
    }
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
  // A previous sleep entry must not leave a cover file open for the whole
  // awake period.
  releaseCover();

  struct tm local = {};
  if (halClock.isAvailable() && halClock.localTime(local) && local.tm_year + 1900 >= 2024) {
    ctx.year = static_cast<uint16_t>(local.tm_year + 1900);
    ctx.month = static_cast<uint8_t>(local.tm_mon + 1);
    ctx.day = static_cast<uint8_t>(local.tm_mday);
    // tm_wday is 0 = Sunday. The grid is Monday-first unless the user opted
    // into a Sunday-first week in Home > Widgets.
    ctx.calendarSundayFirst = SETTINGS.widgetCalendarSundayFirst != 0;
    ctx.todayColumn = static_cast<uint8_t>(ctx.calendarSundayFirst ? local.tm_wday : (local.tm_wday + 6) % 7);
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
  uint8_t cap = todoRowCap(pageHeight);
  // A corrupt/absent value falls back to one row rather than a blank widget.
  const uint8_t wantedRows = SETTINGS.widgetTodoRows < 1 ? 1 : SETTINGS.widgetTodoRows;
  if (wantedRows < cap) cap = wantedRows;
  // Done items stay on the screen as a record of the day unless the user turns
  // that off in Home > Widgets. Stored order is preserved.
  const bool showCompleted = SETTINGS.widgetShowCompleted != 0;
  for (const auto& item : TODO_STORE.getItems()) {
    if (!showCompleted && item.done) continue;
    if (ctx.todoCount < cap) {
      snprintf(ctx.todoText[ctx.todoCount], sizeof(ctx.todoText[ctx.todoCount]), "%s", item.text.c_str());
      ctx.todoDone[ctx.todoCount] = item.done;
      ++ctx.todoCount;
    } else {
      ++ctx.todoOverflow;
    }
  }

  // These widgets open the current book and can decode a cover; skip that IO
  // when the selected widget does not paint it.
  const auto kind = static_cast<CrossPointSettings::SLEEP_WIDGET>(SETTINGS.sleepScreenWidget);
  if (kind == CrossPointSettings::SLEEP_WIDGET_NOW_READING) {
    captureNowReading(resolveBookPath(), ctx);
  } else if (kind == CrossPointSettings::SLEEP_WIDGET_CLIPS) {
    captureClips(ctx);
  }

  // Metadata joins repeated values with punctuation ("Cal Newport;"). Trim it
  // from the display copy only, after every consumer has read the originals.
  trimTrailingSeparators(ctx.bookTitle);
  trimTrailingSeparators(ctx.bookAuthor);
}

void draw(const GfxRenderer& renderer, const CrossPointSettings::SLEEP_WIDGET kind, const Context& ctx) {
  switch (kind) {
    case CrossPointSettings::SLEEP_WIDGET_CALENDAR:
      drawCalendar(renderer, ctx);
      return;
    case CrossPointSettings::SLEEP_WIDGET_TODO:
      drawTodo(renderer, ctx);
      return;
    case CrossPointSettings::SLEEP_WIDGET_NOW_READING:
      drawNowReading(renderer, ctx);
      return;
    case CrossPointSettings::SLEEP_WIDGET_CLIPS:
      drawClips(renderer, ctx);
      return;
    case CrossPointSettings::SLEEP_WIDGET_NONE:
    case CrossPointSettings::SLEEP_WIDGET_COUNT:
      return;
  }
}

}  // namespace sleep_widgets
