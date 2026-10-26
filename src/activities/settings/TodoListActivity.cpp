#include "TodoListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <utility>

#include "MappedInputManager.h"
#include "TodoStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
// Entry actions of the per-item option menu, in the order they can appear.
enum ItemAction : int { kToggle, kMoveTop, kMoveBottom, kMoveUp, kMoveDown, kEdit, kDelete };
}  // namespace

TodoListActivity::TodoListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("TodoList", renderer, mappedInput) {}

void TodoListActivity::onEnter() {
  UiListActivity::onEnter();

  // Reload from disk in case the file was edited elsewhere (web UI or SD card).
  TODO_STORE.loadFromFile();
  nav.selected = 0;
  rebuildRowItems();
}

void TodoListActivity::rebuildRowItems() {
  const auto& items = TODO_STORE.getItems();
  const bool showAdd = items.size() < TodoStore::kMaxItems;

  rowLabels_.clear();
  rowLabels_.reserve(items.size() + (showAdd ? 1 : 0));
  for (const auto& item : items) {
    std::string label;
    label.reserve(4 + item.text.size());
    label.append(item.done ? "[x] " : "[ ] ");
    label.append(item.text);
    rowLabels_.push_back(std::move(label));
  }
  if (showAdd) rowLabels_.emplace_back(tr(STR_TODO_ADD));

  rowItems_.clear();
  rowItems_.reserve(rowLabels_.size());
  for (size_t i = 0; i < rowLabels_.size(); i++) {
    fui::ListItem row;
    row.label = rowLabels_[i].c_str();
    row.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(row);
  }
  // Keep the always-present Add row useful when there is nothing to list.
  if (showAdd && items.empty()) rowItems_.back().subtitle = tr(STR_TODO_EMPTY);
}

bool TodoListActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

const char* TodoListActivity::headerTitle() const { return tr(STR_TODO_LIST); }

void TodoListActivity::activateIndex(const int index) {
  nav.selected = index;
  // Activation opens an editor or popup; a lingering flash would gray an
  // unrelated row.
  app.clearTapFlash();
  handleSelection();
  requestUpdate();
}

void TodoListActivity::handleSelection() {
  if (nav.selected < 0) return;
  const size_t index = static_cast<size_t>(nav.selected);
  const auto& items = TODO_STORE.getItems();
  if (index < items.size()) {
    showItemOptions(index);
  } else if (index == items.size() && items.size() < TodoStore::kMaxItems) {
    addItem();
  }
}

void TodoListActivity::addItem() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TODO_ADD), std::string(),
                                                           TodoStore::kMaxTextBytes, InputType::Text);
  if (!keyboard) {
    LOG_ERR("TODO", "OOM: todo add keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      TODO_STORE.addItem(std::get<KeyboardResult>(result.data).text);
    }
    rebuildRowItems();
  });
}

void TodoListActivity::editItem(const size_t index) {
  const auto& items = TODO_STORE.getItems();
  if (index >= items.size()) return;

  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TODO_EDIT), items[index].text,
                                                           TodoStore::kMaxTextBytes, InputType::Text);
  if (!keyboard) {
    LOG_ERR("TODO", "OOM: todo edit keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, index](const ActivityResult& result) {
    if (!result.isCancelled) {
      TODO_STORE.updateItem(index, std::get<KeyboardResult>(result.data).text);
    }
    rebuildRowItems();
  });
}

void TodoListActivity::showItemOptions(const size_t index) {
  const auto& items = TODO_STORE.getItems();
  if (index >= items.size()) return;
  const TodoItem& item = items[index];

  const StrId toggleLabel = item.done ? StrId::STR_TODO_MARK_UNDONE : StrId::STR_TODO_MARK_DONE;

  // Built list so a move entry only exists where the row can actually move.
  std::vector<std::string> labels;
  std::vector<int> actions;
  labels.reserve(7);
  actions.reserve(7);

  labels.emplace_back(I18N.get(toggleLabel));
  actions.push_back(kToggle);
  if (index > 0) {
    labels.emplace_back(tr(STR_TODO_MOVE_TOP));
    actions.push_back(kMoveTop);
    labels.emplace_back(tr(STR_TODO_MOVE_UP));
    actions.push_back(kMoveUp);
  }
  if (index + 1 < items.size()) {
    labels.emplace_back(tr(STR_TODO_MOVE_DOWN));
    actions.push_back(kMoveDown);
    labels.emplace_back(tr(STR_TODO_MOVE_BOTTOM));
    actions.push_back(kMoveBottom);
  }
  labels.emplace_back(tr(STR_TODO_EDIT));
  actions.push_back(kEdit);
  labels.emplace_back(tr(STR_TODO_DELETE));
  actions.push_back(kDelete);

  std::vector<const char*> ptrs;
  ptrs.reserve(labels.size());
  for (const auto& label : labels) ptrs.push_back(label.c_str());

  optionPopup.show(item.text.c_str(), ptrs.data(), static_cast<int>(ptrs.size()), 0,
                   [this, index, actions](const int selected) {
                     if (selected < 0 || selected >= static_cast<int>(actions.size())) return;
                     const auto& current = TODO_STORE.getItems();
                     if (index >= current.size()) return;
                     switch (actions[selected]) {
                       case kToggle:
                         TODO_STORE.setDone(index, !current[index].done);
                         rebuildRowItems();
                         requestUpdate();
                         break;
                       case kMoveTop:
                       case kMoveBottom:
                       case kMoveUp:
                       case kMoveDown: {
                         size_t newIndex = index;
                         if (actions[selected] == kMoveUp) {
                           newIndex = index - 1;
                         } else if (actions[selected] == kMoveDown) {
                           newIndex = index + 1;
                         } else if (actions[selected] == kMoveTop) {
                           newIndex = 0;
                         } else {
                           newIndex = current.size() - 1;
                         }
                         TODO_STORE.moveItem(index, newIndex);
                         nav.selected = static_cast<int>(newIndex);
                         rebuildRowItems();
                         requestUpdate();
                         break;
                       }
                       case kEdit:
                         editItem(index);
                         break;
                       case kDelete:
                         confirmDelete(index);
                         break;
                     }
                   });
  requestUpdate();
}

void TodoListActivity::confirmDelete(const size_t index) {
  const auto& items = TODO_STORE.getItems();
  if (index >= items.size()) return;

  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, items[index].text, std::string(),
                                                              std::string(), tr(STR_TODO_DELETE));
  if (!confirmation) {
    LOG_ERR("TODO", "OOM: todo delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this, index](const ActivityResult& result) {
    if (!result.isCancelled) TODO_STORE.removeItem(index);
    rebuildRowItems();
  });
}

void TodoListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints; derived
  // from the safe area so board bezel insets apply (same as the OPDS list).
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void TodoListActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  UiListActivity::render(std::move(lock));
}
