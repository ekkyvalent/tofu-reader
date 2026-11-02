#pragma once

#include <array>
#include <string>
#include <vector>

#include "HomeCoverCache.h"
#include "RecentBooksStore.h"
#include "UiAppHost.h"
#include "components/bars/tab-bar.h"
#include "components/media/book-card.h"
#include "components/media/cover-grid.h"

class CoverGridHomeUi final : public UiAppHost {
 public:
  static constexpr int THUMB_HEIGHT = 400;
  static constexpr int GRID_COLUMNS = 3;
  // One page is either the hero plus six cells or nine cells; nine is the
  // maximum number of books resolved into a page.
  static constexpr int MAX_BOOKS = 9;
  static_assert(MAX_BOOKS <= HomeCoverCache::MAX_COVERS);

  // One release from the interaction table, tagged with the band it came from.
  struct HomeGesture {
    enum class Kind : uint8_t { None, Book, Filter, Bottom } kind = Kind::None;
    int index = -1;
    bool longPress = false;
  };

  explicit CoverGridHomeUi(GfxRenderer& renderer);
  void begin(const std::vector<RecentBook>& books, bool hasOpds, bool hasContinueReading);
  void refreshCoverPaths();
  // HomeActivity owns the selection state; this mirrors it for one paint.
  // `focus` follows HomeActivity::HomeBand (0 Filter, 1 Books, 2 Bottom).
  void setSelection(int filter, int filterIndex, int bookIndex, int bottomIndex, int focus, bool hasHero,
                    bool pageResolved);
  HomeGesture takeGesture(const MappedInputManager& input);
  // Exact generation height, shared by every slot and recorded during draw.
  // Thumbs must be generated at the drawn size: rescaling a dithered 1-bit
  // image aliases badly.
  int thumbHeightFor() const;
  bool takeThumbHeightChanged();

 private:
  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  void draw(UiScreen& screen);
  void prepareCard(freeink::ui::ThemeTokens const& theme);
  void drawHeaderBand();
  void drawFilter(UiScreen& screen, freeink::ui::Rect rect);
  void drawFilterBand(UiScreen& screen, freeink::ui::Rect rect);
  void drawEmpty(UiScreen& screen, const char* title, const char* message);
  void drawCurrent(UiScreen& screen, freeink::ui::Rect rect);
  void drawGrid(UiScreen& screen, int rows);
  freeink::ui::Rect layoutGrid(freeink::ui::Rect rect, int rows);
  void drawTabs(UiScreen& screen, freeink::ui::Rect rect);
  bool paintFramedCover(freeink::ui::DrawTarget& target, freeink::ui::Rect rect, size_t index);
  void refreshCoverPath(size_t index);
  void noteThumbHeight(int slotHeight);
  const char* emptyTitle() const;
  const char* emptyMessage() const;

  HomeCoverCache coverCache;
  GfxRenderer& renderer;
  const std::vector<RecentBook>* books = nullptr;
  std::array<std::string, MAX_BOOKS> coverPaths;
  std::array<bool, MAX_BOOKS> readFlags{};
  int thumbHeight = 0;
  bool thumbHeightChanged = false;
  bool coverCacheStarted = false;
  // Selection mirror, refreshed by setSelection() before every paint.
  int filterValue = 0;
  int filterPos = 0;
  int bookPos = 0;
  int bottomPos = 0;
  int focusBand = 1;
  bool hasHero = false;
  bool pageResolved = false;
  int progress = -1;
  bool hasOpds = false;
  bool hasContinueReading = false;
  char progressText[12]{};
  // Badge paints come from the resolved normal cell style (theme ink), never
  // from a literal black/white.
  freeink::ui::Paint readBandPaint{};
  freeink::ui::TextStyle readText{};
  // Resolved once per draw from the render target; the cover painter has no
  // target of its own to query.
  int16_t readLineHeight = 0;
  // Component styles and interaction tables stay off the render task's stack.
  freeink::ui::BookCardProps card;
  freeink::ui::CoverGridProps grid;
  freeink::ui::Rect gridBounds{};
  freeink::ui::TabBarProps tabs;
  std::array<freeink::ui::TabItem, 6> tabItems;
};
