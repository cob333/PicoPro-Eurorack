#include <PicoSystemRuntime.h>
#include <cassert>
#include <cstdio>
#include <random>
uint8_t picoProAudioRoutingMode = PICO_BOOT_AUDIO_STEREO;
uint8_t picoProScreenRotationMode = PICO_BOOT_SCREEN_NORMAL;
struct Display { uint8_t rotation = 0; void setRotation(uint8_t r) { rotation = r; } };
int main() {
  std::mt19937 random(42);
  for (unsigned i = 0; i < 10000; ++i) {
    const int32_t a = static_cast<int32_t>(random()), b = static_cast<int32_t>(random());
    int32_t left = a, right = b;
    picoProAudioRoutingMode = PICO_BOOT_AUDIO_STEREO;
    PicoProRouteAudioInput(left, right);
    assert(left == a && right == b);
    picoProAudioRoutingMode = PICO_BOOT_AUDIO_MONO_X2;
    PicoProRouteAudioInput(left, right);
    const int32_t expected = (static_cast<int64_t>(a) + b) / 2;
    assert(left == expected && right == expected);
    PicoProRouteAudioInput(left, right);
    assert(left == expected && right == expected);
  }
  int32_t left = INT32_MAX, right = INT32_MAX;
  PicoProRouteAudioInput(left, right); assert(left == INT32_MAX && right == INT32_MAX);
  left = right = INT32_MIN;
  PicoProRouteAudioInput(left, right); assert(left == INT32_MIN && right == INT32_MIN);
  left = INT32_MIN; right = INT32_MAX;
  PicoProRouteAudioInput(left, right); assert(left == 0 && right == 0);
  Display display;
  PicoProApplyDisplayRotation(display); assert(display.rotation == 0);
  picoProScreenRotationMode = PICO_BOOT_SCREEN_REVERSE;
  PicoProApplyDisplayRotation(display); assert(display.rotation == 2);
  puts("Runtime stereo identity, mono overflow/headroom and rotation tests passed");
}
