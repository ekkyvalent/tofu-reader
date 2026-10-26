#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Home > Widgets: the sleep-screen widget picker plus its theme and per-widget
// options, and the entry point to the todo editor. Owns the rows that used to
// live under Settings > Display so the widget controls are one tap from Home.
class WidgetsActivity final : public UiListActivity {
 public:
  WidgetsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  enum Row : int {
    kChoose,
    kTheme,
    kTodo,
    kShowCompleted,
    kTodoRows,
    kTextSize,
    kCalendarSunday,
    kRowCount,
  };

  int listCount() const override { return kRowCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;
  const char* headerTitle() const override;

  OptionPopup optionPopup;

  // Row labels are built once; the value column is refreshed every buildScreen
  // by assigning into the existing strings so the pointers stay valid.
  std::vector<std::string> rowLabels_;
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;

  void rebuildRowItems();
  void refreshValues();
  void chooseWidget();
  void chooseTheme();
  void openTodoList();
  void chooseTodoRows();
  void chooseTextSize();
};
