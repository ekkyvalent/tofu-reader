#include "FrontlightPresetsActivity.h"

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;
namespace fp = frontlight_presets;

namespace {
constexpr int16_t kSliderRowHeight = 56;

uint8_t percentFromPermille(const int16_t permille) {
  int value = (static_cast<int>(permille) * 100 + 500) / 1000;
  if (value < 0) value = 0;
  if (value > 100) value = 100;
  return static_cast<uint8_t>(value);
}

// Step buttons carry the row and direction in one action value.
int16_t stepValue(const int row, const bool increment) { return static_cast<int16_t>(row * 2 + (increment ? 1 : 0)); }
}  // namespace

void FrontlightPresetsActivity::onEnter() {
  UiListActivity::onEnter();
  app.on(ACTION_BRIGHTNESS, &FrontlightPresetsActivity::onSliderEvent, this);
  app.on(ACTION_WARMTH, &FrontlightPresetsActivity::onSliderEvent, this);
  app.on(ACTION_STEP, &FrontlightPresetsActivity::onSliderEvent, this);
  editSlot = NO_SLOT;
  dirty = false;
  draggingSlider = false;
}

void FrontlightPresetsActivity::onExit() {
  if (editSlot != NO_SLOT) leaveEdit();
  UiListActivity::onExit();
}

bool FrontlightPresetsActivity::isCustom() const {
  return editSlot != NO_SLOT && SETTINGS.*fp::MODE_FIELDS[editSlot] == fp::MODE_CUSTOM;
}

int FrontlightPresetsActivity::editRowCount() const {
  if (!isCustom()) return 1;
  return Frontlight.hasColorTemperature() ? 3 : 2;
}

const char* FrontlightPresetsActivity::headerTitle() const {
  if (editSlot == NO_SLOT) return tr(STR_FRONTLIGHT_PRESETS);
  return title;
}

void FrontlightPresetsActivity::onBackButton() {
  if (editSlot != NO_SLOT) {
    leaveEdit();
    requestUpdate();
    return;
  }
  finish();
}

void FrontlightPresetsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (editSlot != NO_SLOT) {
    editRow = ROW_MODE;
    toggleMode();
    return;
  }
  if (index < 0 || index >= fp::SLOT_COUNT) return;
  nav.selected = index;
  enterEdit(index);
}

void FrontlightPresetsActivity::enterEdit(const int slot) {
  mappedInput.resetHomeButtonInput();
  liveOn = Frontlight.isOn();
  liveBrightness = Frontlight.brightness();
  liveWarmth = Frontlight.warmth();
  editSlot = slot;
  editRow = ROW_MODE;
  draggingSlider = false;
  snprintf(title, sizeof(title), "%s %d", tr(STR_PRESET), slot + 1);
  previewSlot();
  closeRouting();
  requestUpdate();
}

void FrontlightPresetsActivity::leaveEdit() {
  // Previewing moved the hardware; put it back so the light matches the
  // saved state until a preset is actually applied.
  Frontlight.setBrightness(liveBrightness);
  if (Frontlight.hasColorTemperature()) Frontlight.setWarmth(liveWarmth);
  Frontlight.setOn(liveOn);
  if (dirty) {
    SETTINGS.saveToFile();
    dirty = false;
  }
  editSlot = NO_SLOT;
  draggingSlider = false;
  closeRouting();
}

void FrontlightPresetsActivity::previewSlot() const {
  if (!isCustom()) {
    Frontlight.setOn(false);
    return;
  }
  Frontlight.setBrightness(SETTINGS.*fp::BRIGHTNESS_FIELDS[editSlot]);
  if (Frontlight.hasColorTemperature()) Frontlight.setWarmth(SETTINGS.*fp::WARMTH_FIELDS[editSlot]);
  Frontlight.setOn(true);
}

void FrontlightPresetsActivity::toggleMode() {
  uint8_t& mode = SETTINGS.*fp::MODE_FIELDS[editSlot];
  mode = mode == fp::MODE_OFF ? fp::MODE_CUSTOM : fp::MODE_OFF;
  dirty = true;
  if (editRow >= editRowCount()) editRow = ROW_MODE;
  previewSlot();
  requestUpdate();
}

void FrontlightPresetsActivity::setValue(const int row, const uint8_t percent) {
  if (!isCustom()) return;
  uint8_t& field = SETTINGS.*(row == ROW_BRIGHTNESS ? fp::BRIGHTNESS_FIELDS[editSlot] : fp::WARMTH_FIELDS[editSlot]);
  if (field == percent) return;
  field = percent;
  dirty = true;
  previewSlot();
}

void FrontlightPresetsActivity::adjust(const int row, const int delta) {
  if (row == ROW_MODE) {
    toggleMode();
    return;
  }
  if (!isCustom() || (row == ROW_WARMTH && !Frontlight.hasColorTemperature())) return;
  const uint8_t current =
      SETTINGS.*(row == ROW_BRIGHTNESS ? fp::BRIGHTNESS_FIELDS[editSlot] : fp::WARMTH_FIELDS[editSlot]);
  int next = static_cast<int>(current) + delta;
  if (next < 0) next = 0;
  if (next > 100) next = 100;
  setValue(row, static_cast<uint8_t>(next));
  requestUpdate();
}

void FrontlightPresetsActivity::onSliderEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<FrontlightPresetsActivity*>(user);
  if (self->editSlot == NO_SLOT) return;
  switch (event.action) {
    case ACTION_BRIGHTNESS:
      if (event.dragPermille >= 0) self->setValue(ROW_BRIGHTNESS, percentFromPermille(event.dragPermille));
      break;
    case ACTION_WARMTH:
      if (event.dragPermille >= 0) self->setValue(ROW_WARMTH, percentFromPermille(event.dragPermille));
      break;
    case ACTION_STEP:
      self->adjust(event.value / 2, (event.value % 2) ? 1 : -1);
      break;
    default:
      break;
  }
}

void FrontlightPresetsActivity::loop() {
  if (editSlot == NO_SLOT) {
    UiListActivity::loop();
    return;
  }

  const auto touch = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (touch.routed) {
    if (app.invalidated()) requestUpdate();
    if (touch) {
      if (touch.event.dragPermille >= 0) draggingSlider = true;
      return;
    }
  }
  if (draggingSlider) {
    if (!touch.snap.touchHeld) {
      draggingSlider = false;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (editRow == ROW_MODE) toggleMode();
    return;
  }

  ButtonNavigator::onNextPress([this] {
    editRow = (editRow + 1) % editRowCount();
    requestUpdate();
  });
  ButtonNavigator::onPreviousPress([this] {
    editRow = (editRow + editRowCount() - 1) % editRowCount();
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjust(editRow, -1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjust(editRow, 1); });
}

void FrontlightPresetsActivity::drawFooter() {
  if (editSlot == NO_SLOT) {
    UiListActivity::drawFooter();
    return;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), editRow == ROW_MODE ? tr(STR_TOGGLE) : "", "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FrontlightPresetsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (editSlot == NO_SLOT) {
    buildListScreen(screen);
  } else {
    buildEditScreen(screen);
  }
}

void FrontlightPresetsActivity::formatRowLabel(const int slot, char* out, const size_t size) const {
  if (SETTINGS.*fp::MODE_FIELDS[slot] == fp::MODE_OFF) {
    snprintf(out, size, "%s %d · %s", tr(STR_PRESET), slot + 1, tr(STR_PRESET_OFF));
  } else if (Frontlight.hasColorTemperature()) {
    snprintf(out, size, "%s %d · %u%% · %s %u", tr(STR_PRESET), slot + 1,
             static_cast<unsigned>(SETTINGS.*fp::BRIGHTNESS_FIELDS[slot]), tr(STR_WARMTH),
             static_cast<unsigned>(SETTINGS.*fp::WARMTH_FIELDS[slot]));
  } else {
    snprintf(out, size, "%s %d · %u%%", tr(STR_PRESET), slot + 1,
             static_cast<unsigned>(SETTINGS.*fp::BRIGHTNESS_FIELDS[slot]));
  }
}

void FrontlightPresetsActivity::buildListScreen(UiScreen& screen) {
  for (int i = 0; i < fp::SLOT_COUNT; ++i) {
    formatRowLabel(i, rowLabels[i], sizeof(rowLabels[i]));
    rows[i].label = rowLabels[i];
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  fui::ListProps props;
  props.items = rows;
  props.count = fp::SLOT_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}

void FrontlightPresetsActivity::addSliderRow(UiScreen& screen, const char* label, const uint8_t value,
                                             const int row, const fui::ActionId action) {
  // Drawn before this call returns, so stack buffers are safe to borrow.
  char caption[40];
  snprintf(caption, sizeof(caption), "%s%s", editRow == row ? "> " : "", label);
  char pct[8];
  snprintf(pct, sizeof(pct), "%u%%", static_cast<unsigned>(value));

  sliderProps.label = caption;
  sliderProps.value = pct;
  sliderProps.sliderValue = value;
  sliderProps.sliderAction = action;
  sliderProps.decrement = ACTION_STEP;
  sliderProps.increment = ACTION_STEP;
  sliderProps.decrementValue = stepValue(row, false);
  sliderProps.incrementValue = stepValue(row, true);
  sliderProps.toggleAction = fui::NO_ACTION;
  sliderProps.toggleIcon = fui::BitmapRef{};
  screen.sliderRow(sliderProps, kSliderRowHeight);
  screen.spacer(screen.theme().spaceMd);
}

void FrontlightPresetsActivity::buildEditScreen(UiScreen& screen) {
  modeRow.label = tr(STR_PRESET_MODE);
  modeRow.value = isCustom() ? tr(STR_PRESET_CUSTOM) : tr(STR_PRESET_OFF);
  modeRow.actionValue = 0;

  fui::ListProps props;
  props.items = &modeRow;
  props.count = 1;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.selectedIndex = editRow == ROW_MODE ? 0 : -1;
  screen.list(props, screen.theme().rowHeight);
  screen.spacer(screen.theme().spaceMd);

  if (!isCustom()) return;
  addSliderRow(screen, tr(STR_BRIGHTNESS), SETTINGS.*fp::BRIGHTNESS_FIELDS[editSlot], ROW_BRIGHTNESS, ACTION_BRIGHTNESS);
  if (Frontlight.hasColorTemperature()) {
    addSliderRow(screen, tr(STR_WARMTH), SETTINGS.*fp::WARMTH_FIELDS[editSlot], ROW_WARMTH, ACTION_WARMTH);
  }
}
