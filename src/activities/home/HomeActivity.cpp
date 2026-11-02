#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReadStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

int HomeActivity::getMenuItemCount() const {
  int count = 5;  // File Browser, Library, File transfer, Widgets, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasLibrarySlot()) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(coverGridUi ? maxBooks : std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::resolveGridCoverPaths() {
  for (auto& book : recentBooks) {
    if (!book.coverBmpPath.empty()) continue;
    // Constructors only derive cache paths; no metadata parsing or image generation.
    // Keep these large objects off the task stack and release each before the next book.
    if (FsHelpers::hasReflowableBookExtension(book.path)) {
      auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
      if (!epub) {
        LOG_ERR("HOME", "OOM: EPUB thumbnail path");
        continue;
      }
      book.coverBmpPath = epub->getThumbBmpPath();
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
      if (!xtc) {
        LOG_ERR("HOME", "OOM: XTC thumbnail path");
        continue;
      }
      book.coverBmpPath = xtc->getThumbBmpPath();
    }
  }
}

void HomeActivity::loadGridCover(RecentBook& book, int height, bool& showingLoading, Rect& popupRect) {
  if (!book.coverBmpPath.empty() && Storage.exists(UITheme::getCoverThumbPath(book.coverBmpPath, height).c_str()))
    return;
  // Only one parser lives at a time; EPUB/XTC objects exceed the stack budget.
  if (FsHelpers::hasReflowableBookExtension(book.path)) {
    auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("HOME", "OOM: cover EPUB");
      return;
    }
    book.coverBmpPath = epub->getThumbBmpPath();
    if (Storage.exists(epub->getThumbBmpPath(height).c_str())) return;
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
    }
    if (epub->generateThumbBmpFromSource(height)) {
      return;
    }
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("HOME", "OOM: cover XTC");
      return;
    }
    book.coverBmpPath = xtc->getThumbBmpPath();
    if (Storage.exists(xtc->getThumbBmpPath(height).c_str())) return;
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
    }
    if (xtc->load() && xtc->generateThumbBmp(height)) {
      return;
    }
  }
  book.coverBmpPath.clear();
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    // The cover grid shares one slot size; generating at any other height
    // would rescale the dithered thumb at draw time and alias badly.
    const int thumbHeight = coverGridUi ? coverGridUi->thumbHeightFor() : coverHeight;
    if (coverGridUi) {
      loadGridCover(book, thumbHeight, showingLoading, popupRect);
      ++progress;
      if (showingLoading) GUI.fillPopupProgress(renderer, popupRect, progress * 100 / recentBooks.size());
      continue;
    }
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub/txt/md, try to load the metadata for title/author and cover
        if (FsHelpers::hasReflowableBookExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(thumbHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(thumbHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();
  // Plugin System is the global opt-in. Once enabled, expose the Plugins entry
  // even when no SD plugins are installed yet so Plugin Hub can be installed
  // from inside the Plugins screen (or users can sideload plugins manually).
  hasPlugins = SETTINGS.pluginsEnabled != 0;

  const auto& metrics = UITheme::getInstance().getMetrics();
  if (UITheme::getInstance().hasCoverGridHome()) {
    // Screen-lifetime interaction tables and component properties exceed the stack budget.
    coverGridUi = makeUniqueNoThrow<CoverGridHomeUi>(renderer);
    if (!coverGridUi) {
      LOG_ERR("HOME", "OOM: cover grid UI; using standard home");
    } else {
      // First paint shows the loading slot; loop() resolves the page after it.
      // The page walk lives in resolvePage(), outside the render path.
      recentBooks.clear();
      filter = HomeFilter::Home;
      focus = initialMenuItem == HomeMenuItem::NONE ? HomeBand::Books : HomeBand::Bottom;
      filterIndex = 0;
      bookIndex = 0;
      bottomIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : menuItemToIndex(initialMenuItem, hasLibrarySlot());
      pageIndex = 0;
      hasHero = false;
      pageExhausted = false;
      pageCursor = 0;
      pageResolved = false;
      resolvedFilter = HomeFilter::Home;
      resolvedPage = -1;
      hasContinueReading = false;
      coverGridUi->begin(recentBooks, hasLibrarySlot(), false);
    }
  }
  if (!coverGridUi) {
    loadRecentBooks(metrics.homeRecentBooksCount);
    hasContinueReading = !recentBooks.empty();
  }

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasLibrarySlot());

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  coverGridUi.reset();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

int HomeActivity::bottomMenuCount() const { return 5 + (hasLibrarySlot() ? 1 : 0); }

int HomeActivity::pageSizeFor(const int page) const {
  return filter == HomeFilter::Home && page == 0 ? 1 + kHomeHeroPage : kGridPage;
}

int HomeActivity::rowsForPage() const { return filter == HomeFilter::Home && pageIndex == 0 ? 2 : 3; }

void HomeActivity::resolvePage() {
  if (!coverGridUi) {
    pageResolved = true;
    return;
  }
  const int pageSize = pageSizeFor(pageIndex);
  // A forward step continues from the row after the last resolved page; every
  // other request (backwards, filter change, read-state change) rescans from
  // the start and skips the accepted books of the earlier pages.
  const bool forward = filter == resolvedFilter && pageIndex == resolvedPage + 1;
  const bool useCursor = forward && (filter != HomeFilter::Home || pageIndex >= 2);
  const int startRow = useCursor ? static_cast<int>(pageCursor) : 0;
  int skip = 0;
  if (!useCursor) {
    if (filter == HomeFilter::Home) {
      skip = pageIndex == 0 ? 0 : (kHomeHeroPage + 1) + (pageIndex - 1) * kGridPage;
    } else {
      skip = pageIndex * kGridPage;
    }
  }

  std::vector<RecentBook> page;
  page.reserve(pageSize);

  // Keep the index and record together off the task stack; reuse for every row.
  struct LibraryReader {
    library::LibraryIndexFile index;
    library::ClixRecord record;
  };
  auto reader = makeUniqueNoThrow<LibraryReader>();
  bool indexReady = false;
  if (reader) {
    if (reader->index.open(library::libraryIndexPath())) {
      indexReady = true;
    } else {
      reader->index.close();
      library::BuildStats stats;
      if (library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0)) {
        indexReady = reader->index.open(library::libraryIndexPath());
      }
    }
  }
  if (!indexReady) LOG_ERR("HOME", "Cannot read the library index");

  int nextRow = startRow;
  bool streamEnded = !indexReady;
  const auto take = [&page, &skip, pageSize](RecentBook&& book) {
    if (skip > 0) {
      --skip;
      return false;
    }
    page.push_back(std::move(book));
    return static_cast<int>(page.size()) >= pageSize;
  };

  std::vector<std::string> taken;
  const auto isTaken = [&taken](const std::string& path) {
    return std::find(taken.begin(), taken.end(), path) != taken.end();
  };

  bool filled = false;
  if (filter == HomeFilter::Home && !useCursor) {
    // Page 0 and the first paged walk: most-recent-first, not-read, no
    // duplicates. The index rows below continue from this order.
    for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
      if (READ_STORE.isRead(book.path) || RecentBooksStore::isMissing(book) || isTaken(book.path)) continue;
      taken.push_back(book.path);
      RecentBook copy = book;
      if (take(std::move(copy))) {
        filled = true;
        break;
      }
    }
  }

  if (!filled && indexReady && reader) {
    const int rows = reader->index.bookCount();
    for (int row = startRow; row < rows; ++row) {
      nextRow = row + 1;
      const uint16_t ordinal =
          reader->index.ordinalForRow(library::SortOrder::RecentDesc, static_cast<uint16_t>(row));
      if (ordinal == 0xFFFF) break;
      RecentBook book;
      if (!reader->index.readRecord(ordinal, reader->record)) {
        if (reader->index.ioFailed()) {
          streamEnded = true;
          break;
        }
        continue;
      }
      if (!reader->index.readPath(reader->record, book.path)) continue;
      if (filter == HomeFilter::Home) {
        if (READ_STORE.isRead(book.path) || isTaken(book.path)) continue;
      } else if (filter == HomeFilter::Archived) {
        if (!READ_STORE.isRead(book.path)) continue;
      }
      if (!reader->index.readTitle(reader->record, book.title) || book.title.empty()) {
        reader->index.readName(reader->record, book.title);
      }
      reader->index.readAuthor(reader->record, book.author);
      if (book.title.empty()) book.title = tr(STR_LIBRARY_UNKNOWN_TITLE);
      if (take(std::move(book))) {
        filled = true;
        streamEnded = row + 1 >= rows;
        break;
      }
    }
    if (!filled) streamEnded = true;
  }

  if (page.empty() && pageIndex > 0) {
    // The walk ended on the previous page boundary: step back and let the
    // next loop resolve the page that does exist.
    --pageIndex;
    bookIndex = 0;
    pageExhausted = true;
    pageResolved = false;
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    recentBooks = std::move(page);
    hasHero = filter == HomeFilter::Home && pageIndex == 0 && !recentBooks.empty();
    hasContinueReading = hasHero;
    if (bookIndex >= static_cast<int>(recentBooks.size())) {
      bookIndex = recentBooks.empty() ? 0 : static_cast<int>(recentBooks.size()) - 1;
    }
    resolveGridCoverPaths();
    coverGridUi->begin(recentBooks, hasLibrarySlot(), hasHero);
    recentsLoaded = false;
    pageResolved = true;
    halfRepaintPending = true;
    resolvedFilter = filter;
    resolvedPage = pageIndex;
    pageCursor = static_cast<uint16_t>(nextRow);
    pageExhausted = streamEnded;
  }
  requestUpdate(true);
}

void HomeActivity::selectFilter(const int index) {
  const int next = std::clamp(index, 0, 2);
  filterIndex = next;
  focus = HomeBand::Filter;
  const auto nextFilter = static_cast<HomeFilter>(next);
  if (filter == nextFilter) {
    requestUpdate();
    return;
  }
  filter = nextFilter;
  pageIndex = 0;
  pageCursor = 0;
  resolvedPage = -1;
  bookIndex = 0;
  pageExhausted = false;
  pageResolved = false;
  recentsLoaded = false;
  halfRepaintPending = true;
  resolvePage();
}

bool HomeActivity::turnPage(const int dir) {
  if (dir < 0 && pageIndex == 0) return false;
  if (dir > 0 && pageExhausted) return false;
  const int target = pageIndex + dir;
  pageIndex = target;
  pageResolved = false;
  resolvePage();
  return pageIndex == target;
}

void HomeActivity::swallowHeldReleases() {
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  lockNextBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
}

void HomeActivity::activateMenuItem(const int menuIndex) {
  switch (indexToMenuItem(menuIndex, hasLibrarySlot())) {
    case HomeMenuItem::FILE_BROWSER:
      onFileBrowserOpen();
      break;
    case HomeMenuItem::LIBRARY:
      onLibraryOpen();
      break;
    case HomeMenuItem::OPDS_BROWSER:  // the library slot
      hasPlugins ? onPluginsOpen() : onOpdsBrowserOpen();
      break;
    case HomeMenuItem::FILE_TRANSFER:
      onFileTransferOpen();
      break;
    case HomeMenuItem::WIDGETS_MENU:
      onWidgetsOpen();
      break;
    case HomeMenuItem::SETTINGS_MENU:
      onSettingsOpen();
      break;
    default:
      break;
  }
}

void HomeActivity::activateFocused() {
  if (focus == HomeBand::Filter) {
    selectFilter(filterIndex);
    return;
  }
  if (focus == HomeBand::Bottom) {
    activateMenuItem(bottomIndex);
    return;
  }
  if (bookIndex >= 0 && bookIndex < static_cast<int>(recentBooks.size())) {
    onRecentBookSelected(recentBooks[bookIndex].path);
  }
}

void HomeActivity::showBookOptions(const int index) {
  if (index < 0 || index >= static_cast<int>(recentBooks.size())) return;
  const std::string path = recentBooks[index].path;
  const std::string title = recentBooks[index].title;
  const bool read = READ_STORE.isRead(path);
  const char* options[] = {tr(STR_OPEN), read ? tr(STR_MARK_AS_UNREAD) : tr(STR_MARK_AS_READ)};
  optionPopup.show(tr(STR_LIBRARY), title.c_str(), options, 2, 0, [this, path](const int choice) {
    swallowHeldReleases();
    if (choice == 0) {
      onRecentBookSelected(path);
      return;
    }
    if (READ_STORE.markRead(path, !READ_STORE.isRead(path))) {
      // The book may leave or join this filter; rescan from the first row.
      resolvedPage = -1;
      pageResolved = false;
      bookIndex = 0;
      resolvePage();
    } else {
      LOG_ERR("HOME", "cannot update read state for %s", path.c_str());
    }
  });
  requestUpdate();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  auto activateSelection = [this] {
    if (selectorIndex < recentBooks.size()) {
      onRecentBookSelected(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, hasLibrarySlot())) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::LIBRARY:
        onLibraryOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:  // the library slot
        hasPlugins ? onPluginsOpen() : onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::WIDGETS_MENU:
        onWidgetsOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  // Cover grid home splits navigation by band; the flat next/previous cycle
  // is for the classic list home only.
  if (!coverGridUi) {
    buttonNavigator.onNext([this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onPrevious([this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });
  }

  if (coverGridUi) {
    if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    // The release that chose a popup option must not also drive the page.
    if (lockNextConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      lockNextConfirmRelease = false;
      return;
    }
    if (lockNextBackRelease && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      lockNextBackRelease = false;
      return;
    }

    // Page resolution runs outside the render path, after the first paint.
    if (firstRenderDone && !pageResolved) {
      resolvePage();
      return;
    }

    const auto gesture = coverGridUi->takeGesture(mappedInput);
    if (gesture.kind != CoverGridHomeUi::HomeGesture::Kind::None) {
      switch (gesture.kind) {
        case CoverGridHomeUi::HomeGesture::Kind::Filter:
          selectFilter(gesture.index);
          return;
        case CoverGridHomeUi::HomeGesture::Kind::Book:
          if (gesture.index < 0 || gesture.index >= static_cast<int>(recentBooks.size())) return;
          bookIndex = gesture.index;
          focus = HomeBand::Books;
          if (gesture.longPress) {
            showBookOptions(gesture.index);
            return;
          }
          requestUpdate();
          onRecentBookSelected(recentBooks[gesture.index].path);
          return;
        case CoverGridHomeUi::HomeGesture::Kind::Bottom:
          if (gesture.index < 0 || gesture.index >= bottomMenuCount()) return;
          bottomIndex = gesture.index;
          focus = HomeBand::Bottom;
          activateMenuItem(gesture.index);
          return;
        case CoverGridHomeUi::HomeGesture::Kind::None:
          break;
      }
    }

    // Back is otherwise unused on the home menu: open the featured book (the
    // resolved page's hero is the most recent one, as before).
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) && hasContinueReading && !recentBooks.empty()) {
      onRecentBookSelected(recentBooks.front().path);
      return;
    }

    // Left/Right move within the focused band. At a grid row edge the move
    // turns the page; the filter band wraps 0..2.
    const auto moveWithinBand = [this](const int dir) {
      if (focus == HomeBand::Filter) {
        filterIndex = (filterIndex + 3 + dir) % 3;
      } else if (focus == HomeBand::Bottom) {
        const int count = bottomMenuCount();
        if (count > 0) bottomIndex = (bottomIndex + count + dir) % count;
      } else {
        const int count = static_cast<int>(recentBooks.size());
        if (count <= 0) return;
        const int offset = hasHero ? 1 : 0;
        if (bookIndex < offset) return;  // the hero row has a single cell
        const int cell = bookIndex - offset;
        const int col = cell % CoverGridHomeUi::GRID_COLUMNS;
        if (dir > 0) {
          if (col + 1 < CoverGridHomeUi::GRID_COLUMNS && cell + 1 < count - offset) {
            ++bookIndex;
          } else if (turnPage(1)) {
            bookIndex = hasHero ? 1 : 0;
          }
        } else {
          if (col > 0) {
            --bookIndex;
          } else if (turnPage(-1)) {
            // The previous page resolved; land on its last cell.
            bookIndex =
                static_cast<int>(recentBooks.size()) > 1 ? static_cast<int>(recentBooks.size()) - 1 : 0;
          }
        }
      }
      requestUpdate();
    };

    // Up/Down move between bands at a band edge, otherwise within the grid
    // rows/columns.
    const auto moveVertical = [this](const int dir) {
      if (dir < 0) {
        if (focus == HomeBand::Bottom) {
          focus = recentBooks.empty() ? HomeBand::Filter : HomeBand::Books;
        } else if (focus == HomeBand::Books) {
          const int offset = hasHero ? 1 : 0;
          if (bookIndex < offset || (bookIndex - offset) / CoverGridHomeUi::GRID_COLUMNS == 0) {
            focus = HomeBand::Filter;
          } else {
            bookIndex -= CoverGridHomeUi::GRID_COLUMNS;
          }
        }
      } else {
        if (focus == HomeBand::Filter) {
          focus = recentBooks.empty() ? HomeBand::Bottom : HomeBand::Books;
        } else if (focus == HomeBand::Books) {
          const int count = static_cast<int>(recentBooks.size());
          const int offset = hasHero ? 1 : 0;
          if (count <= offset) {
            focus = HomeBand::Bottom;
          } else if (hasHero && bookIndex < offset) {
            bookIndex = offset;
          } else {
            const int cell = bookIndex - offset;
            const int row = cell / CoverGridHomeUi::GRID_COLUMNS;
            if (row + 1 < rowsForPage() && cell + CoverGridHomeUi::GRID_COLUMNS < count - offset) {
              bookIndex += CoverGridHomeUi::GRID_COLUMNS;
            } else {
              focus = HomeBand::Bottom;
            }
          }
        }
      }
      requestUpdate();
    };

    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left},
                                         [&moveWithinBand] { moveWithinBand(-1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right},
                                         [&moveWithinBand] { moveWithinBand(1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [&moveVertical] { moveVertical(-1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [&moveVertical] { moveVertical(1); });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      activateFocused();
    }
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && hasContinueReading && !recentBooks.empty()) {
    onRecentBookSelected(recentBooks[0].path);
    return;
  }

  // Match the rendered cover grid, which lays tiles inside getContentArea()
  // (origin content.x + contentSidePadding, span content.width). Using
  // screen-relative getScreenWidth()/contentSidePadding here offset the touch
  // targets from the drawn covers by content.x on bezel-inset panels (EEGO A4).
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  const int coverColumnCount = std::max(1, metrics.homeRecentBooksCount);
  const int recentCount = std::min(static_cast<int>(recentBooks.size()), coverColumnCount);
  const int coverColumnWidth = (content.width - 2 * metrics.contentSidePadding) / coverColumnCount;
  int touchedBook = -1;
  const auto coverTouch = mappedInput.colTouch(touchedBook, content.x + metrics.contentSidePadding, coverColumnWidth,
                                               recentCount, metrics.homeTopPadding,
                                               metrics.homeTopPadding + metrics.homeCoverTileHeight, coverColumnWidth);
  if (coverTouch != MappedInputManager::RowTouch::None) {
    if (coverTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedBook) {
        selectorIndex = touchedBook;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedBook;
      haptic_feedback::touchAction();
      activateSelection();
    }
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int renderedMenuCount =
      menuCount - (metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size()));
  int menuRow = -1;
  // Row height from the theme, not the metrics table: RoundedRaff draws
  // font-derived rows and the touch grid must match the visuals exactly.
  const int menuRowHeight = GUI.getMenuRowHeight(renderer);
  const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, menuRowHeight + metrics.menuSpacing, renderedMenuCount,
                                              0, INT32_MAX, menuRowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        metrics.homeContinueReadingInMenu ? menuRow : menuRow + static_cast<int>(recentBooks.size());
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      haptic_feedback::touchAction();
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  // Modal option popup draws over the previous frame (same plumbing as the
  // file browser), so it must run before clearScreen().
  if (optionPopup.processRender(renderer, mappedInput)) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  // Content columns clear the bezel insets on rounded panels. The header
  // self-insets in drawHeader, so only the cover/menu need it here.
  const Rect content = UITheme::getInstance().getContentArea(renderer);

  renderer.clearScreen();
  if (coverGridUi) {
    coverGridUi->setSelection(static_cast<int>(filter), filterIndex, bookIndex, bottomIndex,
                              static_cast<int>(focus), hasHero, pageResolved);
    UITheme::getInstance().drawCoverGridHome(*coverGridUi);
    // Up/Down move between bands, Left/Right within the focused band.
    const auto labels = mappedInput.mapLabels(hasContinueReading ? tr(STR_RESUME) : "", tr(STR_SELECT),
                                              tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    const bool firstFrame = cleanInitialRefresh && !firstRenderDone;
    renderer.displayBuffer(firstFrame || halfRepaintPending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    halfRepaintPending = false;
    // Slot heights are recorded during the draw above; a change (first layout
    // pass, orientation switch) means the paths must point at those sizes and
    // any missing thumbs must be generated. Refreshing the paths right away
    // lets the next pass draw already-cached thumbs before generation runs.
    const bool coverSpecChanged = coverGridUi->takeThumbHeightChanged();
    if (coverSpecChanged) {
      coverGridUi->refreshCoverPaths();
      recentsLoaded = false;
    }
    if (!firstRenderDone) {
      firstRenderDone = true;
      requestUpdate();
    } else if (!recentsLoaded && !recentsLoading) {
      loadRecentCovers(CoverGridHomeUi::THUMB_HEIGHT);
      coverGridUi->refreshCoverPaths();
      halfRepaintPending = true;
      requestUpdate();
    }
    return;
  }
  bool bufferRestored = coverRendered && coverBufferStored && restoreCoverBuffer();

  // Band spans topPadding..homeTopPadding: the cover tile starts at the fixed
  // homeTopPadding, so the height must shrink by topPadding or the band (and a
  // centered title, e.g. RoundedRaff's book title) sinks into the tile.
  // Home is the stack root: no back button in its header.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr,
                 nullptr, false);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = content.x;
  coverRectY = metrics.homeTopPadding;
  coverRectW = content.width;
  coverRectH = metrics.homeCoverTileHeight;

  GUI.drawRecentBookCover(renderer, Rect{content.x, metrics.homeTopPadding, content.width, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_LIBRARY), tr(STR_FILE_TRANSFER), tr(STR_WIDGETS),
                                        tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Library, Transfer, Widgets, Settings};

  if (hasLibrarySlot()) {
    menuItems.insert(menuItems.begin() + 2, hasPlugins ? tr(STR_PLUGINS) : tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Plugins);
  }

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{content.x, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, content.width,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    const int themeThumbHeight = GUI.homeCoverThumbHeight(renderer);
    loadRecentCovers(themeThumbHeight > 0 ? themeThumbHeight : metrics.homeCoverHeight);
  }
}

void HomeActivity::onRecentBookSelected(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onWidgetsOpen() { activityManager.goToWidgets(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onPluginsOpen() { activityManager.goToPlugins(hasOpdsServers); }
