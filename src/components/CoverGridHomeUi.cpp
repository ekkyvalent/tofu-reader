#include "CoverGridHomeUi.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "ReadStore.h"
#include "UITheme.h"
#include "icons/blocks.h"
#include "icons/book.h"
#include "icons/folder.h"
#include "icons/library.h"
#include "icons/settings2.h"
#include "icons/transfer.h"
#include "icons/widgets.h"
#include "util/BookProgress.h"

namespace fui = freeink::ui;

namespace {
enum : fui::ActionId { ACTION_BOOK = 1, ACTION_FILTER = 2, ACTION_BOTTOM = 3 };
// Grid cell padding around each cover; also feeds the screen's horizontal
// inset so the cover columns land on the header chrome's inset line.
constexpr int16_t COVER_CELL_INSET = 6;
// Filter values mirror HomeActivity::HomeFilter.
constexpr int FILTER_HOME = 0;
constexpr int FILTER_ARCHIVED = 1;
constexpr int FILTER_ALL = 2;
}  // namespace

CoverGridHomeUi::CoverGridHomeUi(GfxRenderer& renderer)
    : UiAppHost(renderer), coverCache(renderer), renderer(renderer) {}

void CoverGridHomeUi::begin(const std::vector<RecentBook>& recent, bool opds, bool continuing) {
  books = &recent;
  hasOpds = opds;
  hasContinueReading = continuing;
  readFlags.fill(false);
  for (size_t i = 0; i < recent.size() && i < readFlags.size(); ++i) {
    readFlags[i] = READ_STORE.isRead(recent[i].path);
  }
  if (!recent.empty() && !coverCacheStarted) {
    coverCacheStarted = true;
    coverCache.begin();
  }
  resetUi();
  app.on(ACTION_BOOK, &CoverGridHomeUi::onAction, this);
  app.on(ACTION_FILTER, &CoverGridHomeUi::onAction, this);
  app.on(ACTION_BOTTOM, &CoverGridHomeUi::onAction, this);
  app.setScreen(&CoverGridHomeUi::screenFn, this);
  refreshCoverPaths();
  progress = hasContinueReading && !books->empty() ? loadBookProgress(books->front().path) : -1;
  if (progress >= 0) snprintf(progressText, sizeof(progressText), "%d%%", progress);
}

void CoverGridHomeUi::refreshCoverPaths() {
  coverCache.invalidate();
  for (size_t i = 0; i < books->size() && i < coverPaths.size(); ++i) refreshCoverPath(i);
}

void CoverGridHomeUi::refreshCoverPath(size_t index) {
  if (index >= books->size() || index >= coverPaths.size()) return;
  coverCache.invalidate(index);
  coverPaths[index] =
      thumbHeight > 0 ? UITheme::getCoverThumbPath((*books)[index].coverBmpPath, thumbHeight) : std::string();
}

int CoverGridHomeUi::thumbHeightFor() const { return thumbHeight > 0 ? thumbHeight : THUMB_HEIGHT; }

bool CoverGridHomeUi::takeThumbHeightChanged() { return std::exchange(thumbHeightChanged, false); }

void CoverGridHomeUi::noteThumbHeight(int slotHeight) {
  // One shared height for every slot (hero and grid covers are the same
  // size). The +8 surplus gives the paint clip a little bleed so the art's
  // rightward nudge never exposes the left edge. The grid asks for the same
  // 0.6 width/height ratio, so 3:5 art lands in the slot without side slivers.
  const int height = std::max(1, slotHeight + 8);
  if (thumbHeight != height) {
    thumbHeight = height;
    thumbHeightChanged = true;
    refreshCoverPaths();
  }
}

void CoverGridHomeUi::setSelection(const int filter, const int filterIndex, const int bookIndex,
                                   const int bottomIndex, const int focus, const bool hero, const bool resolved) {
  filterValue = filter;
  filterPos = filterIndex;
  bookPos = bookIndex;
  bottomPos = bottomIndex;
  focusBand = focus;
  hasHero = hero;
  pageResolved = resolved;
}

void CoverGridHomeUi::onAction(const fui::ActionEvent&, void* user) {
  auto& self = *static_cast<CoverGridHomeUi*>(user);
  // The rendered selection ring is the cursor; a gray tap flash on top of it
  // is noise (the old single-action grid cleared the flash here too).
  self.app.clearTapFlash();
}

CoverGridHomeUi::HomeGesture CoverGridHomeUi::takeGesture(const MappedInputManager& input) {
  HomeGesture gesture;
  const auto touch = routeTouch(input, /*withLongPress=*/true);
  if (!touch.snap.touchReleased || !touch.event) return gesture;
  switch (touch.event.action) {
    case ACTION_BOOK:
      gesture.kind = HomeGesture::Kind::Book;
      break;
    case ACTION_FILTER:
      gesture.kind = HomeGesture::Kind::Filter;
      break;
    case ACTION_BOTTOM:
      gesture.kind = HomeGesture::Kind::Bottom;
      break;
    default:
      return gesture;
  }
  gesture.index = touch.event.value;
  gesture.longPress = touch.event.longPress;
  return gesture;
}

void CoverGridHomeUi::screenFn(UiScreen& screen, void* user) { static_cast<CoverGridHomeUi*>(user)->draw(screen); }

void CoverGridHomeUi::draw(UiScreen& screen) {
  coverCache.prepare();
  const auto& theme = screen.theme();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  // Sides clear the rounded corners like the header, so the grid lines up under it.
  const int corner = BoardConfig::ACTIVE.cornerRadius;
  const auto left = static_cast<int16_t>(std::max(safe.x, corner));
  const auto right = static_cast<int16_t>(std::max(renderer.getScreenWidth() - safe.x - safe.width, corner));
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(safe.y), right,
                                                static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height),
                                                left});
  // Horizontal inset sized so the outer cover columns (content x plus the
  // grid's cell inset) sit on the theme's side-padding line: the heading and
  // tabs then align with the clock and battery, which tuck a few px further
  // in (headerStatusInset's optical bias).
  const int16_t hInset =
      std::max<int16_t>(0, static_cast<int16_t>(metrics.headerSidePadding - COVER_CELL_INSET - left));
  // Drop the status band, hero, and grid down: touch boards get a full step
  // (no button-hint band to fit), button boards a modest nudge. The tabs stay
  // put (bottom-anchored below).
  const int16_t topInset =
      BoardConfig::hasTouch() ? theme.spaceSm : static_cast<int16_t>(theme.spaceSm + 4);
  screen.insetContent(fui::Insets{topInset, hInset, theme.spaceSm, hInset});
  const bool landscape = renderer.getScreenWidth() > renderer.getScreenHeight();
  // Reserve the band's slot in the flow (its content draws at a fixed screen
  // position in drawHeaderBand); the slot doubles as padding above the heading.
  screen.takeTop(static_cast<int16_t>(metrics.batteryBarHeight), theme.spaceSm);
  // Text filter row: HOME / ARCHIVED / ALL. Grow the row to the label's line
  // height so the glyphs never overflow into the underline, and start the
  // covers right under it.
  const int16_t filterRowHeight = std::max<int16_t>(
      metrics.coverGridFilterRowHeight, screen.target().lineHeight(theme.smallText.font) + 4);
  fui::Rect filterRect = screen.takeTop(filterRowHeight, theme.spaceSm);
  // Button boards run tighter above the tabs to make room for the top step.
  const int16_t tabGap = BoardConfig::hasTouch() ? theme.spaceSm : static_cast<int16_t>(4);
  const auto& board = BoardConfig::ACTIVE;
  auto tabRect = screen.takeBottom(static_cast<int16_t>(metrics.coverGridTabBarHeight), tabGap);
  // The board's tab height sits at the bottom of the theme's slot, then its
  // offset lower; the slot itself is unchanged, so the grid keeps its size.
  const auto tabHeight = static_cast<int16_t>(tabRect.height * board.tabBarHeightPct / 100);
  tabRect.y = static_cast<int16_t>(tabRect.y + tabRect.height - tabHeight + board.tabBarOffsetY);
  tabRect.height = tabHeight;
  // The board's share of the row the tabs spread across, centered.
  const auto fitTabWidth = [&board](fui::Rect rect) {
    const auto width = static_cast<int16_t>(rect.width * board.tabBarWidthPct / 100);
    rect.x = static_cast<int16_t>(rect.x + (rect.width - width) / 2);
    rect.width = width;
    return rect;
  };

  if (!pageResolved) {
    drawFilterBand(screen, filterRect);
    drawTabs(screen, fitTabWidth(tabRect.inset(fui::Insets{0, COVER_CELL_INSET, 0, COVER_CELL_INSET})));
    drawEmpty(screen, tr(STR_LIBRARY_LOADING), nullptr);
    drawHeaderBand();
    return;
  }
  if (books->empty()) {
    drawFilterBand(screen, filterRect);
    drawTabs(screen, fitTabWidth(tabRect.inset(fui::Insets{0, COVER_CELL_INSET, 0, COVER_CELL_INSET})));
    drawEmpty(screen, emptyTitle(), emptyMessage());
    drawHeaderBand();
    return;
  }

  prepareCard(theme);
  readLineHeight = screen.target().lineHeight(readText.font);
  const fui::Rect body = screen.body();
  const int rowGap = std::max<int>(4, body.width / 100);
  grid.gap = grid.rowGap = static_cast<int16_t>(rowGap);
  // Hero and grid covers share the same size. The body always holds three
  // such rows: the hero plus two grid rows, or three grid rows. Derive the
  // cover from the height left after the filter row's gap and the 12px
  // label/padding floor so the new row cannot push the grid into the tab bar.
  const int rows = hasHero ? 2 : 3;
  const int chromeGaps = (rows - 1) * rowGap + (hasHero ? theme.spaceSm : 0);
  const int maxCoverWidth = body.width / GRID_COLUMNS - 2 * COVER_CELL_INSET;
  // Fit the shared 3:5 cover box within both the row and a grid column. No
  // gap reserve: the cell insets already keep adjacent covers apart, and the
  // SpaceBetween layout absorbs whatever slack remains. The thumbnail
  // generator targets the same 0.6 width/height ratio, so 3:5 art fills the
  // box instead of leaving side slivers.
  card.coverSize.height =
      std::max(1, std::min((body.height - chromeGaps) / 3 - 12, maxCoverWidth * 5 / 3));
  card.coverSize.width = std::max(1, card.coverSize.height * 3 / 5);
  const int coverRowHeight = card.coverSize.height + 12;
  const int featuredHeight = std::min<int>(
      body.height, std::max<int>(coverRowHeight, screen.target().lineHeight(theme.bodyText.font) * (landscape ? 1 : 2) +
                                                     screen.target().lineHeight(theme.smallText.font) * 2 + 32));
  noteThumbHeight(card.coverSize.height);

  fui::Rect gridArea = body;
  if (hasHero) {
    const auto heroRect = screen.takeTop(static_cast<int16_t>(featuredHeight), theme.spaceSm);
    drawCurrent(screen, heroRect);
    gridArea = screen.body();
  }
  gridBounds = layoutGrid(gridArea, rows);
  drawGrid(screen, rows);

  tabRect.x = static_cast<int16_t>(gridBounds.x + grid.cellInset.left);
  tabRect.width = static_cast<int16_t>(gridBounds.width - grid.cellInset.left - grid.cellInset.right);
  drawFilterBand(screen, filterRect);
  drawTabs(screen, fitTabWidth(tabRect));
  drawHeaderBand();
}

void CoverGridHomeUi::prepareCard(const fui::ThemeTokens& theme) {
  const bool landscape = renderer.getScreenWidth() > renderer.getScreenHeight();
  card.styles = theme.listRow;
  card.styles.selected.background = fui::Paint::dither(fui::Color::LightGray);
  // The grid thumbs' selection ring draws with this border: gray like Lyra's
  // selection box, not solid black.
  card.styles.selected.border = fui::Paint::dither(fui::Color::LightGray);
  card.styles.selected.foreground = fui::Paint::solid(fui::Color::Black);
  card.styles.selected.radius = theme.listRowRadius;
  card.styles.active = card.styles.selected;
  // The READ badge paints come from the resolved normal cell style: theme ink
  // on a theme-paper label, never a literal black/white.
  const auto badgeStyle = card.styles.resolve(fui::StateNormal);
  readBandPaint = badgeStyle.foreground;
  readText = theme.smallText;
  readText.color =
      badgeStyle.background.kind == fui::PaintKind::None ? fui::Color::White : badgeStyle.background.color;
  readText.align = fui::TextAlign::Center;
  readText.maxLines = 1;

  card.titleText = theme.bodyText;
  card.titleText.maxLines = landscape ? 1 : 2;
  card.authorText = theme.smallText;
  card.progressText = theme.smallText;
  card.progressHeight = 6;
  card.padding = fui::Insets{6, 6, 6, 6};
  card.gap = theme.spaceLg + theme.spaceSm;
}

void CoverGridHomeUi::drawHeaderBand() {
  // The stock full-width band at the theme's topPadding, exactly like every
  // pushed screen's header: the battery/clock hold one position across the
  // whole UI, and the grid is widened to meet them (see the hInset above).
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.batteryBarHeight}, nullptr);
}

void CoverGridHomeUi::drawFilter(UiScreen& screen, fui::Rect rect) {
  // Full reset: `tabs` is shared with drawTabs(), and a leftover iconPainter
  // makes tabBar() reserve a 16 px icon slot above every label (see
  // tab-bar.h: hasIcon = props.icon || props.iconPainter != nullptr), which
  // pushes the labels into the underline. This only showed up from the second
  // frame on, i.e. after the bottom bar had been drawn once.
  tabs = fui::TabBarProps{};
  static constexpr StrId LABELS[3] = {StrId::STR_HOME_TAB, StrId::STR_ARCHIVED_TAB, StrId::STR_ALL_TAB};
  for (int i = 0; i < 3; ++i) {
    auto& item = tabItems[i];
    item = fui::TabItem{};
    item.label = I18N.get(LABELS[i]);
    item.value = static_cast<int16_t>(i);
    item.selected = i == filterPos;
  }
  tabs.tabs = tabItems.data();
  tabs.count = 3;
  tabs.layout = fui::TabBarLayout::SpaceBetween;
  tabs.action = ACTION_FILTER;
  tabs.inputMask = fui::InputTouch;
  // No pill fill: mark the style set as explicitly provided so tabBar() does
  // not fall back to its default black pill. The theme still decides the label
  // font (tabs.text) and the underline thickness (metric below).
  tabs.tabStyles = {};
  tabs.tabStyles.explicitlySet = true;
  tabs.iconSize = 0;
  // drawTabs() shares this props object and installs a bottom-bar painter;
  // clear it or these tabs inherit that icon on the next frame.
  tabs.iconPainter = nullptr;
  tabs.iconPainterUserData = nullptr;
  tabs.text = screen.theme().smallText;
  tabs.distributedSlotWidth = 0;
  tabs.selectedUnderline = static_cast<int16_t>(UITheme::getInstance().getMetrics().coverGridFilterUnderline);
  fui::tabBar(screen.frame(), rect, tabs);
}

void CoverGridHomeUi::drawFilterBand(UiScreen& screen, fui::Rect rect) {
  // Chrome band: repaint its paper so grid/cover content cannot leave pixels here.
  const auto paint = screen.theme().listRow.normal.background;
  screen.target().fill(rect, paint.kind == fui::PaintKind::None ? fui::Paint::solid(fui::Color::White) : paint);
  drawFilter(screen, rect);
}

const char* CoverGridHomeUi::emptyTitle() const {
  switch (filterValue) {
    case FILTER_ARCHIVED:
      return tr(STR_ARCHIVED_EMPTY_TITLE);
    case FILTER_ALL:
      return tr(STR_ALL_EMPTY_TITLE);
    case FILTER_HOME:
    default:
      return tr(STR_HOME_EMPTY_TITLE);
  }
}

const char* CoverGridHomeUi::emptyMessage() const {
  switch (filterValue) {
    case FILTER_ARCHIVED:
      return tr(STR_ARCHIVED_EMPTY_MESSAGE);
    case FILTER_ALL:
      return tr(STR_ALL_EMPTY_MESSAGE);
    case FILTER_HOME:
    default:
      return tr(STR_HOME_EMPTY_MESSAGE);
  }
}

void CoverGridHomeUi::drawEmpty(UiScreen& screen, const char* title, const char* message) {
  const auto& theme = screen.theme();
  const auto body = screen.body();
  auto titleStyle = theme.titleText;
  titleStyle.bold = true;
  titleStyle.align = fui::TextAlign::Center;
  auto messageStyle = theme.bodyText;
  messageStyle.align = fui::TextAlign::Center;
  const auto titleHeight = static_cast<int16_t>(screen.target().lineHeight(titleStyle.font));
  const auto messageHeight = static_cast<int16_t>(message ? screen.target().lineHeight(messageStyle.font) : 0);
  const auto contentHeight =
      static_cast<int16_t>(titleHeight + (message ? theme.spaceSm + messageHeight : 0));
  int16_t y = static_cast<int16_t>(body.y + std::max(0, (body.height - contentHeight) / 2));
  screen.target().text(fui::Rect{body.x, y, body.width, titleHeight}, title, titleStyle);
  if (message) {
    y = static_cast<int16_t>(y + titleHeight + theme.spaceSm);
    screen.target().text(fui::Rect{body.x, y, body.width, messageHeight}, message, messageStyle);
  }
}

void CoverGridHomeUi::drawCurrent(UiScreen& screen, fui::Rect rect) {
  const auto& book = books->front();
  card.title = book.title.c_str();
  card.author = book.author.empty() ? nullptr : book.author.c_str();
  card.meta = nullptr;
  card.progressLabel = progress >= 0 ? progressText : nullptr;
  card.centerTextOnCover = true;
  card.progress = std::max(0, progress);
  card.progressMax = progress >= 0 ? 100 : 0;
  card.action = ACTION_BOOK;
  card.value = 0;
  // The featured card's selected state is a slim accent bar drawn after the
  // card (see below), not a bookCard indicator: every ring/outline treatment
  // tried here either overwhelmed the large cover or made the heading above
  // read as misaligned.
  card.state = fui::StateNormal;
  card.coverPainterUserData = this;
  card.coverPainter = [](fui::DrawTarget& target, fui::Rect cover, const fui::BookCardProps&, void* user) {
    return static_cast<CoverGridHomeUi*>(user)->paintFramedCover(target, cover, 0);
  };
  fui::bookCard(screen.frame(), rect, card);

  if (focusBand == 1 && bookPos == 0 && !BoardConfig::hasTouch()) {
    // Button boards only: a vertical accent bar left of the card, cover-height
    // and vertically centered on it, marks the featured card as the button
    // cursor without framing the cover. Touch boards tap directly and need no
    // cursor on the hero card.
    const auto barH = static_cast<int16_t>(card.coverSize.height);
    screen.target().fill(
        fui::Rect{static_cast<int16_t>(rect.x - 5), static_cast<int16_t>(rect.y + (rect.height - barH) / 2), 3, barH},
        fui::Paint::dither(fui::Color::LightGray));
  }
}

fui::Rect CoverGridHomeUi::layoutGrid(fui::Rect rect, int rows) {
  // Exactly the hero's box: the three-equal-rows split in draw() already
  // guarantees it fits, and sharing the size keeps one cached thumb per book.
  grid.cellInset = fui::Insets{COVER_CELL_INSET, COVER_CELL_INSET, COVER_CELL_INSET, COVER_CELL_INSET};
  grid.coverSize = card.coverSize;
  grid.rowHeight = static_cast<int16_t>(grid.coverSize.height + 12);
  // Full content width: the SpaceBetween column layout pins the outer covers
  // to the rect edges, so the grid reaches the chrome's inset line instead of
  // centering at its natural width.
  rect.height = static_cast<int16_t>(rows * grid.rowHeight + (rows - 1) * grid.rowGap);
  return rect;
}

void CoverGridHomeUi::drawGrid(UiScreen& screen, int rows) {
  const auto rect = gridBounds;
  const int offset = hasHero ? 1 : 0;
  const int total = static_cast<int>(books->size());
  grid.count = total > offset ? static_cast<uint16_t>(total - offset) : 0;
  grid.columns = GRID_COLUMNS;
  grid.columnLayout = fui::CoverGridColumnLayout::SpaceBetween;
  grid.action = ACTION_BOOK;
  // Long press is the alternate output for a book (open the options popup);
  // it uses the same action id and is read from ActionEvent::longPress.
  grid.inputMask = fui::InputTouch | fui::InputLongPress;
  const int selected =
      focusBand == 1 && bookPos >= offset ? static_cast<int>(bookPos - offset) : -1;
  grid.selectedIndex = static_cast<int16_t>(selected);
  // Same thick cover ring as the featured card; the dithered Cell background
  // was easy to miss behind a dark cover.
  grid.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  // Thick dithered ring sized for the small thumbs: 6px outside the cover,
  // 2px over its edge.
  grid.selectedCoverFrameGap = 6;
  grid.selectedCoverFrameWidth = 8;
  grid.cellStyles = card.styles;
  grid.labelHeight = 0;
  grid.labelGap = 0;
  grid.scrollIndicator = false;
  // A page is exactly what fits: count never exceeds the visible cells, so
  // the grid window always starts at its first row. Thread the row count
  // through the SDK helper so topIndex follows the normal grid contract.
  const auto pageItems = static_cast<uint16_t>(rows * GRID_COLUMNS);
  grid.topIndex = fui::coverGridTopIndexFor(static_cast<uint16_t>(selected < 0 ? 0 : selected), grid.count,
                                            GRID_COLUMNS, pageItems);
  grid.itemProviderUserData = this;
  grid.itemProvider = [](uint16_t index, void* user) {
    const auto& self = *static_cast<CoverGridHomeUi*>(user);
    return fui::coverGridItem(nullptr, static_cast<int>(index) + (self.hasHero ? 1 : 0));
  };
  grid.coverPainterUserData = this;
  grid.coverPainter = [](fui::DrawTarget& target, fui::Rect cover, const fui::CoverGridItem&, uint16_t index,
                         void* user) {
    auto& self = *static_cast<CoverGridHomeUi*>(user);
    return self.paintFramedCover(target, cover, static_cast<size_t>(index) + (self.hasHero ? 1 : 0));
  };
  fui::coverGrid(screen.frame(), rect, grid);
}

void CoverGridHomeUi::drawTabs(UiScreen& screen, fui::Rect rect) {
  // Same full reset as drawFilter(): the two share one props struct, so a field
  // left over from the other bar (labels, underline, text style) must not leak.
  tabs = fui::TabBarProps{};
  static constexpr const uint8_t* ICONS[] = {FolderIcon,   LibraryIcon, BlocksIcon,
                                             TransferIcon, WidgetsIcon, Settings2Icon};
  int count = 0;
  for (int i = 0; i < 6; ++i) {
    if (i == 2 && !hasOpds) continue;
    auto& tab = tabItems[count];
    tab = fui::TabItem{};
    tab.value = static_cast<int16_t>(count);
    tab.selected = focusBand == 2 && bottomPos == count;
    tab.label = nullptr;
    ++count;
  }
  tabs.tabs = tabItems.data();
  tabs.count = static_cast<uint8_t>(count);
  tabs.layout = fui::TabBarLayout::SpaceBetween;
  tabs.action = ACTION_BOTTOM;
  tabs.inputMask = fui::InputTouch;
  tabs.iconSize = 32;
  tabs.iconPainterUserData = this;
  tabs.iconPainter = [](fui::DrawTarget&, fui::Rect iconRect, const fui::TabItem& tab, uint8_t, void* user) {
    const auto& self = *static_cast<CoverGridHomeUi*>(user);
    const int menuIndex = tab.value;
    const int icon = !self.hasOpds && menuIndex >= 2 ? menuIndex + 1 : menuIndex;
    self.renderer.drawIcon(ICONS[icon], iconRect.x, iconRect.y, iconRect.width);
    return true;
  };
  // No pill fill; the underline marks the selected tab.
  tabs.tabStyles = {};
  tabs.tabStyles.explicitlySet = true;
  tabs.selectedUnderline = 2;
  tabs.distributedSlotWidth = 0;
  fui::tabBar(screen.frame(), rect, tabs);
}

bool CoverGridHomeUi::paintFramedCover(fui::DrawTarget& target, fui::Rect rect, size_t index) {
  constexpr int16_t SHADOW_OFFSET = 2;
  const auto ink = fui::Paint::solid(fui::Color::Black);
  target.fill(fui::Rect{rect.right(), static_cast<int16_t>(rect.y + SHADOW_OFFSET), SHADOW_OFFSET, rect.height}, ink);
  target.fill(fui::Rect{static_cast<int16_t>(rect.x + SHADOW_OFFSET), rect.bottom(), rect.width, SHADOW_OFFSET}, ink);
  // The false spine draws inside the cover paint (HomeCoverCache), glued to
  // the art's left edge, so it stays aligned whatever each cover's margin is.
  const bool drawn = index < coverPaths.size() && coverCache.paint(rect, index, coverPaths[index]);
  if (drawn && index < readFlags.size() && readFlags[index]) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int16_t lineH = readLineHeight;
    const int16_t bandH = static_cast<int16_t>(
        std::min(std::max<int>(metrics.readBadgeHeight, lineH + 2), rect.height / 4));
    if (bandH > 0) {
      const fui::Rect band{rect.x, rect.y, rect.width, bandH};
      target.fill(band, readBandPaint);
      // A band shorter than the line would only clip the word: keep the solid
      // marker instead of a half-drawn label.
      if (bandH >= lineH) {
        constexpr int16_t BADGE_INSET = 2;
        const fui::Rect label{static_cast<int16_t>(band.x + BADGE_INSET), static_cast<int16_t>(band.y + BADGE_INSET),
                              static_cast<int16_t>(band.width - 2 * BADGE_INSET),
                              static_cast<int16_t>(band.height - 2 * BADGE_INSET)};
        target.text(label, tr(STR_READ_BADGE), readText);
      }
    }
  }
  return drawn;
}
