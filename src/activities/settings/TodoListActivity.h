#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

/**
 * On-device editor for the todo list shown by the sleep screen widget.
 * Rows are one per stored item followed by "Add item"; selecting an item
 * opens mark done/edit/delete actions.
 */
class TodoListActivity final : public UiListActivity {
 public:
  TodoListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;

  OptionPopup optionPopup;

  // Row labels ("[x] text" / "[ ] text" and "Add item") are built once here
  // so ListItem.label stays valid without per-render copies.
  std::vector<std::string> rowLabels_;
  std::vector<freeink::ui::ListItem> rowItems_;
  void rebuildRowItems();

  void handleSelection();
  void addItem();
  void editItem(size_t index);
  void showItemOptions(size_t index);
  void confirmDelete(size_t index);
};
