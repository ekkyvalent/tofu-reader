#pragma once

#include <cstdint>

#include "CrossPointSettings.h"

// Four fixed frontlight preset slots, each OFF or CUSTOM (brightness + warmth).
// The current slot is never stored: it is derived from the live light state.
namespace frontlight_presets {
inline constexpr uint8_t SLOT_COUNT = 4;
inline constexpr uint8_t MODE_OFF = 0;
inline constexpr uint8_t MODE_CUSTOM = 1;

inline constexpr uint8_t CrossPointSettings::* MODE_FIELDS[SLOT_COUNT] = {
    &CrossPointSettings::frontlightPreset0Mode, &CrossPointSettings::frontlightPreset1Mode,
    &CrossPointSettings::frontlightPreset2Mode, &CrossPointSettings::frontlightPreset3Mode};
inline constexpr uint8_t CrossPointSettings::* BRIGHTNESS_FIELDS[SLOT_COUNT] = {
    &CrossPointSettings::frontlightPreset0Brightness, &CrossPointSettings::frontlightPreset1Brightness,
    &CrossPointSettings::frontlightPreset2Brightness, &CrossPointSettings::frontlightPreset3Brightness};
inline constexpr uint8_t CrossPointSettings::* WARMTH_FIELDS[SLOT_COUNT] = {
    &CrossPointSettings::frontlightPreset0Warmth, &CrossPointSettings::frontlightPreset1Warmth,
    &CrossPointSettings::frontlightPreset2Warmth, &CrossPointSettings::frontlightPreset3Warmth};

// Applies one slot to the hardware and settings, then saves. No repaint.
void applySlot(uint8_t slot);

// Advances 0 -> 1 -> 2 -> 3 -> 0 from the slot matching the live state;
// applies slot 0 when nothing matches. No-op without a frontlight.
void cycle();
}  // namespace frontlight_presets
