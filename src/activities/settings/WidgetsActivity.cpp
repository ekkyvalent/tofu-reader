#include "WidgetsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <iterator>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "TodoListActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
// "Choose a widget" offers the four widget kinds in SLEEP_WIDGET value order,
// starting at CALENDAR (NONE stays reachable by leaving the sleep screen mode).
constexpr StrId kWidgetLabels[] = {StrId::STR_WIDGET_CALENDAR, StrId::STR_WIDGET_TODO, StrId::STR_WIDGET_NOW_READING,
                                   StrId::STR_WIDGET_CLIPS};
constexpr int kWidgetCount = static_cast<int>(std::size(kWidgetLabels));
constexpr uint8_t kFirstWidget = CrossPointSettings::SLEEP_WIDGET_CALENDAR;
// The picker lists every kind except NONE; fail the build if the two drift.
static_assert(kWidgetCount == CrossPointSettings::SLEEP_WIDGET_COUNT - 1,
              "Choose-a-widget labels must track the SLEEP_WIDGET enum");

constexpr StrId kThemeLabels[] = {StrId::STR_LIGHT, StrId::STR_DARK};
constexpr int kThemeCount = static_cast<int>(std::size(kThemeLabels));

constexpr StrId kTextSizeLabels[] = {StrId::STR_WIDGET_TEXT_NORMAL, StrId::STR_WIDGET_TEXT_LARGE};
constexpr int kTextSizeCount = static_cast<int>(std::size(kTextSizeLabels));

constexpr uint8_t kMinTodoRows = 1;
constexpr uint8_t kMaxTodoRows = 8;
}  // namespace

WidgetsActivity::WidgetsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Widgets", renderer, mappedInput) {}

void WidgetsActivity::onEnter() {
  UiListActivity::onEnter();
  nav.selected = 0;
  rebuildRowItems();
}

void WidgetsActivity::rebuildRowItems() {
  rowLabels_.clear();
  rowLabels_.reserve(kRowCount);
  rowLabels_.emplace_back(tr(STR_WIDGET_CHOOSE));
  rowLabels_.emplace_back(tr(STR_WIDGET_THEME));
  rowLabels_.emplace_back(tr(STR_TODO_LIST));
  rowLabels_.emplace_back(tr(STR_WIDGET_SHOW_COMPLETED));
  rowLabels_.emplace_back(tr(STR_WIDGET_TODO_ROWS));
  rowLabels_.emplace_back(tr(STR_WIDGET_TEXT_SIZE));
  rowLabels_.emplace_back(tr(STR_WIDGET_CALENDAR_SUNDAY));

  rowValues_.assign(kRowCount, std::string());

  rowItems_.clear();
  rowItems_.reserve(kRowCount);
  for (int i = 0; i < kRowCount; ++i) {
    fui::ListItem item;
    item.label = rowLabels_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

void WidgetsActivity::refreshValues() {
  const uint8_t widget = SETTINGS.sleepScreenWidget;
  rowValues_[kChoose] = widget >= kFirstWidget && widget < kFirstWidget + kWidgetCount
                            ? I18N.get(kWidgetLabels[widget - kFirstWidget])
                            : tr(STR_WIDGET_NONE);

  const uint8_t theme = SETTINGS.widgetTheme;
  rowValues_[kTheme] = theme < kThemeCount ? I18N.get(kThemeLabels[theme]) : std::string();
  rowValues_[kTodo] = tr(STR_CONFIGURE);
  rowValues_[kShowCompleted] = SETTINGS.widgetShowCompleted ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rowValues_[kTodoRows] = std::to_string(SETTINGS.widgetTodoRows);
  const uint8_t textSize = SETTINGS.widgetTextSize;
  rowValues_[kTextSize] = textSize < kTextSizeCount ? I18N.get(kTextSizeLabels[textSize]) : std::string();
  rowValues_[kCalendarSunday] = SETTINGS.widgetCalendarSundayFirst ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);

  for (int i = 0; i < kRowCount; ++i) {
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }
}

bool WidgetsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

const char* WidgetsActivity::headerTitle() const { return tr(STR_WIDGETS); }

// Back returns to Home with the Widgets row still selected (same as Settings).
void WidgetsActivity::onBackButton() { onGoHome(); }

void WidgetsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  nav.selected = index;
  // Every row opens a popup or a sub-activity; a lingering flash would gray an
  // unrelated element.
  app.clearTapFlash();
  switch (index) {
    case kChoose:
      chooseWidget();
      break;
    case kTheme:
      chooseTheme();
      break;
    case kTodo:
      openTodoList();
      break;
    case kShowCompleted:
      SETTINGS.widgetShowCompleted = SETTINGS.widgetShowCompleted ? 0 : 1;
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case kTodoRows:
      chooseTodoRows();
      break;
    case kTextSize:
      chooseTextSize();
      break;
    case kCalendarSunday:
      SETTINGS.widgetCalendarSundayFirst = SETTINGS.widgetCalendarSundayFirst ? 0 : 1;
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    default:
      break;
  }
}

void WidgetsActivity::chooseWidget() {
  const uint8_t widget = SETTINGS.sleepScreenWidget;
  const int current = widget >= kFirstWidget && widget < kFirstWidget + kWidgetCount ? widget - kFirstWidget : 0;
  optionPopup.show(StrId::STR_WIDGET_CHOOSE, kWidgetLabels, kWidgetCount, current, [this](const int index) {
    if (index < 0 || index >= kWidgetCount) return;
    SETTINGS.sleepScreenWidget = static_cast<uint8_t>(kFirstWidget + index);
    // Picking a widget is meaningless unless the sleep screen shows it, so
    // switch the sleep mode too; the mode can still be changed in Settings.
    SETTINGS.sleepScreen = CrossPointSettings::WIDGET;
    SETTINGS.saveToFile();
    requestUpdate();
  });
  requestUpdate();
}

void WidgetsActivity::chooseTheme() {
  const uint8_t theme = SETTINGS.widgetTheme < kThemeCount ? SETTINGS.widgetTheme : 0;
  optionPopup.show(StrId::STR_WIDGET_THEME, kThemeLabels, kThemeCount, theme, [this](const int index) {
    if (index < 0 || index >= kThemeCount) return;
    SETTINGS.widgetTheme = static_cast<uint8_t>(index);
    SETTINGS.saveToFile();
    requestUpdate();
  });
  requestUpdate();
}

void WidgetsActivity::chooseTextSize() {
  const uint8_t size = SETTINGS.widgetTextSize < kTextSizeCount ? SETTINGS.widgetTextSize : 0;
  optionPopup.show(StrId::STR_WIDGET_TEXT_SIZE, kTextSizeLabels, kTextSizeCount, size, [this](const int index) {
    if (index < 0 || index >= kTextSizeCount) return;
    SETTINGS.widgetTextSize = static_cast<uint8_t>(index);
    SETTINGS.saveToFile();
    requestUpdate();
  });
  requestUpdate();
}

void WidgetsActivity::chooseTodoRows() {
  std::vector<std::string> options;
  options.reserve(kMaxTodoRows - kMinTodoRows + 1);
  for (uint8_t rows = kMinTodoRows; rows <= kMaxTodoRows; ++rows) {
    options.emplace_back(std::to_string(rows));
  }
  const uint8_t rows = SETTINGS.widgetTodoRows;
  const int current = rows >= kMinTodoRows && rows <= kMaxTodoRows ? rows - kMinTodoRows : options.size() - 1;
  optionPopup.show(StrId::STR_WIDGET_TODO_ROWS, options, current, [this](const int index) {
    if (index < 0) return;
    SETTINGS.widgetTodoRows = static_cast<uint8_t>(kMinTodoRows + index);
    SETTINGS.saveToFile();
    requestUpdate();
  });
  requestUpdate();
}

void WidgetsActivity::openTodoList() {
  auto activity = makeUniqueNoThrow<TodoListActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("WIDGETS", "OOM: TodoListActivity");
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
}

void WidgetsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  refreshValues();

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void WidgetsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  UiListActivity::render(std::move(lock));
}
