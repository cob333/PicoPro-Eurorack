#ifndef PICOPRO_SYSTEM_RUNTIME_H
#define PICOPRO_SYSTEM_RUNTIME_H
#include <stdint.h>
#include "PicoBootSystemSettings.h"

// Load in setup, before audio starts. Audio paths only read these atomic bytes;
// they never access configuration flash or initiate persistence.
void PicoProSystemSettingsBegin();
void PicoProApplySystemSettings(const PicoBootSystemSettings &settings);
extern uint8_t picoProAudioRoutingMode;
extern uint8_t picoProScreenRotationMode;

static inline bool PicoProMonoInputEnabled() {
  return __atomic_load_n(&picoProAudioRoutingMode, __ATOMIC_RELAXED) == PICO_BOOT_AUDIO_MONO_X2;
}
static inline bool PicoProScreenReversed() {
  return __atomic_load_n(&picoProScreenRotationMode, __ATOMIC_RELAXED) == PICO_BOOT_SCREEN_REVERSE;
}
static inline void PicoProRouteAudioInput(int32_t &left, int32_t &right) {
  if (!PicoProMonoInputEnabled()) return;
  // Unity gain for identical inputs and no overflow for two full-scale inputs.
  const int32_t mono = static_cast<int32_t>((static_cast<int64_t>(left) + right) / 2);
  left = right = mono;
}
template <typename Display>
static inline void PicoProApplyDisplayRotation(Display &display) {
  display.setRotation(PicoProScreenReversed() ? 2 : 0);
}
#endif
