#ifndef PICOPRO_BOOT_SYSTEM_SETTINGS_H_
#define PICOPRO_BOOT_SYSTEM_SETTINGS_H_
#include <stdint.h>

#define PICO_BOOT_AUDIO_STEREO 0u
#define PICO_BOOT_AUDIO_MONO_X2 1u
#define PICO_BOOT_SCREEN_NORMAL 0u
#define PICO_BOOT_SCREEN_REVERSE 1u
#define PICO_BOOT_SYSTEM_SETTINGS_MASK 0x0003u

typedef struct {
  uint8_t audio_routing;
  uint8_t screen_rotation;
} PicoBootSystemSettings;

static inline int PicoBootSystemSettingsValid(const PicoBootSystemSettings *settings) {
  return settings != 0 && settings->audio_routing <= PICO_BOOT_AUDIO_MONO_X2 &&
         settings->screen_rotation <= PICO_BOOT_SCREEN_REVERSE;
}

static inline PicoBootSystemSettings PicoBootSystemSettingsFromFlags(uint16_t flags) {
  PicoBootSystemSettings settings = {(uint8_t)(flags & 1u), (uint8_t)((flags >> 1) & 1u)};
  return settings;
}

// The caller validates the settings. All unrelated reserved0 bits survive.
static inline uint16_t PicoBootSystemSettingsFlags(uint16_t flags,
                                                  const PicoBootSystemSettings *settings) {
  return (uint16_t)((flags & ~PICO_BOOT_SYSTEM_SETTINGS_MASK) |
                   settings->audio_routing | ((uint16_t)settings->screen_rotation << 1));
}
#endif
