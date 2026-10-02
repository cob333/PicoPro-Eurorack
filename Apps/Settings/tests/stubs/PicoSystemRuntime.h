#ifndef SETTINGS_TEST_RUNTIME_H
#define SETTINGS_TEST_RUNTIME_H
#include <PicoBootConfig.h>
extern PicoBootSystemSettings runtimeSettings;
inline bool PicoProMonoInputEnabled() { return runtimeSettings.audio_routing == PICO_BOOT_AUDIO_MONO_X2; }
inline bool PicoProScreenReversed() { return runtimeSettings.screen_rotation == PICO_BOOT_SCREEN_REVERSE; }
inline void PicoProApplySystemSettings(const PicoBootSystemSettings &s) { runtimeSettings = s; }
template<class Display> inline void PicoProApplyDisplayRotation(Display &d) {
  d.setRotation(runtimeSettings.screen_rotation ? 2 : 0);
}
#endif
