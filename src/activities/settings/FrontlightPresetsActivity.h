#pragma once

#include <cstddef>
#include <cstdint>

#include "FrontlightPresets.h"
#include "activities/UiListActivity.h"

// Settings -> Display -> Frontlight presets. A list of the four preset slots;
// selecting one edits it: a mode row (Off / Custom) and, for Custom, brightness
// and warmth sliders that preview live on the hardware. Leaving the editor puts
// the light back as it was and saves the slot.
class FrontlightPresetsActivity final : public UiListActivity {
 public:
  FrontlightPresetsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("FrontlightPresets", renderer, input) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  static constexpr size_t ROW_LABEL_SIZE = 56;
  static constexpr size_t TITLE_SIZE = 24;
  static constexpr int NO_SLOT = -1;
  static constexpr int ROW_MODE = 0;
  static constexpr int ROW_BRIGHTNESS = 1;
  static constexpr int ROW_WARMTH = 2;
  static constexpr freeink::ui::ActionId ACTION_BRIGHTNESS = ACTION_USER;
  static constexpr freeink::ui::ActionId ACTION_WARMTH = ACTION_USER + 1;
  static constexpr freeink::ui::ActionId ACTION_STEP = ACTION_USER + 2;  // value = row * 2 + (increment ? 1 : 0)

  int editSlot = NO_SLOT;
  int editRow = ROW_MODE;
  bool dirty = false;
  bool draggingSlider = false;
  // Hardware state to restore when the editor closes (edits preview live).
  bool liveOn = false;
  uint8_t liveBrightness = 0;
  uint8_t liveWarmth = 0;

  char rowLabels[frontlight_presets::SLOT_COUNT][ROW_LABEL_SIZE]{};
  char title[TITLE_SIZE]{};
  freeink::ui::ListItem rows[frontlight_presets::SLOT_COUNT]{};
  freeink::ui::ListItem modeRow{};
  // fui::SliderRowProps embeds a 324-byte StyleSet, past the 256-byte local
  // budget, so it lives here and is refilled on each use.
  freeink::ui::SliderRowProps sliderProps;

  int listCount() const override { return editSlot == NO_SLOT ? frontlight_presets::SLOT_COUNT : 1; }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void drawFooter() override;

  static void onSliderEvent(const freeink::ui::ActionEvent& event, void* user);

  void buildListScreen(UiScreen& screen);
  void buildEditScreen(UiScreen& screen);
  void addSliderRow(UiScreen& screen, const char* label, uint8_t value, int row, freeink::ui::ActionId action);
  void formatRowLabel(int slot, char* out, size_t size) const;

  void enterEdit(int slot);
  void leaveEdit();
  void toggleMode();
  void adjust(int row, int delta);
  void setValue(int row, uint8_t percent);
  void previewSlot() const;
  bool isCustom() const;
  int editRowCount() const;
};
