#include "../../../Bootloader/boot_config.h"
#include <stddef.h>
#include <cassert>
#include <cstdio>

static_assert(PICO_BOOT_VERSION == 5, "System settings must retain config v5");
static_assert(sizeof(PicoBootConfig) == 456, "System settings must not expand config ABI");
static_assert(offsetof(PicoBootConfig, reserved0) == 14, "Shared settings flag offset changed");
static_assert(offsetof(PicoBootConfig, calibration) == 20, "Calibration ABI changed");
static_assert(offsetof(PicoBootConfig, apps) == 52, "Application slots ABI changed");
static_assert(offsetof(PicoBootConfig, crc32) == 452, "Checksum ABI changed");

int main() {
  const auto defaults = PicoBootSystemSettingsFromFlags(0);
  assert(defaults.audio_routing == PICO_BOOT_AUDIO_STEREO);
  assert(defaults.screen_rotation == PICO_BOOT_SCREEN_NORMAL);
  puts("BootConfig v5/456-byte ABI and default system modes passed");
}
