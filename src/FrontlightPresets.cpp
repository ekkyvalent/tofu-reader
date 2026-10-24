#include "FrontlightPresets.h"

#include <HalFrontlight.h>
#include <Logging.h>

namespace frontlight_presets {
namespace {
uint8_t clampPercent(const uint8_t value) { return value > 100 ? 100 : value; }

// Returns SLOT_COUNT when no slot matches the live state.
uint8_t currentSlot() {
  const bool lightOn = Frontlight.isOn();
  for (uint8_t slot = 0; slot < SLOT_COUNT; ++slot) {
    const bool off = SETTINGS.*MODE_FIELDS[slot] == MODE_OFF;
    if (!lightOn) {
      if (off) return slot;
      continue;
    }
    if (!off && clampPercent(SETTINGS.*BRIGHTNESS_FIELDS[slot]) == SETTINGS.frontlightBrightness &&
        clampPercent(SETTINGS.*WARMTH_FIELDS[slot]) == SETTINGS.frontlightWarmth) {
      return slot;
    }
  }
  return SLOT_COUNT;
}
}  // namespace

void applySlot(const uint8_t slot) {
  if (slot >= SLOT_COUNT || !Frontlight.present()) return;

  if (SETTINGS.*MODE_FIELDS[slot] == MODE_OFF) {
    Frontlight.setOn(false);
    SETTINGS.frontlightOn = 0;
  } else {
    const uint8_t brightness = clampPercent(SETTINGS.*BRIGHTNESS_FIELDS[slot]);
    const uint8_t warmth = clampPercent(SETTINGS.*WARMTH_FIELDS[slot]);
    Frontlight.setBrightness(brightness);
    if (Frontlight.hasColorTemperature()) Frontlight.setWarmth(warmth);
    Frontlight.setOn(true);
    SETTINGS.frontlightOn = 1;
    SETTINGS.frontlightBrightness = brightness;
    SETTINGS.frontlightWarmth = warmth;
  }
  SETTINGS.saveToFile();
  LOG_INF("LIGHT", "Frontlight preset %u applied", static_cast<unsigned>(slot));
}

void cycle() {
  if (!Frontlight.present()) return;
  const uint8_t current = currentSlot();
  applySlot(current >= SLOT_COUNT ? 0 : static_cast<uint8_t>((current + 1) % SLOT_COUNT));
}
}  // namespace frontlight_presets
