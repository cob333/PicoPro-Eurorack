#include "PicoSystemRuntime.h"
#include "PicoBootConfig.h"

uint8_t picoProAudioRoutingMode = PICO_BOOT_AUDIO_STEREO;
uint8_t picoProScreenRotationMode = PICO_BOOT_SCREEN_NORMAL;

void PicoProApplySystemSettings(const PicoBootSystemSettings &settings) {
  if (settings.audio_routing > PICO_BOOT_AUDIO_MONO_X2 ||
      settings.screen_rotation > PICO_BOOT_SCREEN_REVERSE) return;
  __atomic_store_n(&picoProAudioRoutingMode, settings.audio_routing, __ATOMIC_RELEASE);
  __atomic_store_n(&picoProScreenRotationMode, settings.screen_rotation, __ATOMIC_RELEASE);
}
void PicoProSystemSettingsBegin() {
  PicoBootSystemSettings settings{};
  PicoBootLoadSystemSettings(&settings);
  PicoProApplySystemSettings(settings);
}
