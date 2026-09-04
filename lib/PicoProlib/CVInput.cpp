#include "CVInput.h"

#include "PicoPro_io.h"

namespace {

constexpr uint8_t kInputCount = 4u;
constexpr uint8_t kBurstSamples = 12u;
constexpr uint8_t kSettlingReads = 2u;
constexpr uint16_t kAdcRange = 4096u;

static_assert(kBurstSamples > 2u, "CV burst must retain at least one sample");

struct CVInputState {
  uint16_t cached_raw[kInputCount];
  uint32_t cached_ms[kInputCount];
  bool cached_ready[kInputCount];
  volatile uint8_t role[kInputCount];
};

CVInputState state;

uint8_t clampInput(uint8_t input) {
  return input < kInputCount ? input : 0u;
}

uint16_t acquireRaw(uint8_t input) {
  input = clampInput(input);
  if (!PicoCVInputAnalogAvailable(input)) {
    return state.cached_ready[input] ? state.cached_raw[input] : 0u;
  }

  const uint8_t pin = PicoCVInputPin(input);
  for (uint8_t i = 0; i < kSettlingReads; ++i) {
    (void)analogRead(pin);
  }

  uint32_t sum = 0;
  uint16_t lowest = UINT16_MAX;
  uint16_t highest = 0;
  for (uint8_t i = 0; i < kBurstSamples; ++i) {
    const uint16_t sample = analogRead(pin);
    sum += sample;
    if (sample < lowest) lowest = sample;
    if (sample > highest) highest = sample;
  }
  sum -= lowest;
  sum -= highest;
  const uint8_t kept = kBurstSamples - 2u;
  return (uint16_t)((sum + kept / 2u) / kept);
}

}  // namespace

void PicoCVInputBegin(void) {
  analogReadResolution(12);
  for (uint8_t input = 0; input < kInputCount; ++input) {
    state.cached_ready[input] = false;
    __atomic_store_n(&state.role[input], (uint8_t)PICOPRO_CV_ROLE_ANALOG,
                     __ATOMIC_RELEASE);
  }
}

uint8_t PicoCVInputPin(uint8_t input) {
  static const uint8_t pins[kInputCount] = {AIN0, AIN1, AIN2, AIN3};
  return pins[clampInput(input)];
}

bool PicoCVInputAnalogAvailable(uint8_t input) {
  input = clampInput(input);
  return __atomic_load_n(&state.role[input], __ATOMIC_ACQUIRE) ==
         PICOPRO_CV_ROLE_ANALOG;
}

void PicoCVInputSetDigitalRole(uint8_t input, bool enabled, bool pull_up) {
  input = clampInput(input);
  state.cached_ready[input] = false;
  if (enabled) {
    pinMode(PicoCVInputPin(input), pull_up ? INPUT_PULLUP : INPUT);
  }
  __atomic_store_n(&state.role[input],
                   enabled ? (uint8_t)PICOPRO_CV_ROLE_DIGITAL
                           : (uint8_t)PICOPRO_CV_ROLE_ANALOG,
                   __ATOMIC_RELEASE);
}

void PicoCVInputSelectDigitalRole(int8_t one_based_input, int8_t *previous,
                                  bool pull_up) {
  const int8_t selected =
      one_based_input >= 1 && one_based_input <= 2 ? one_based_input : 0;
  if (previous != nullptr && *previous == selected) return;
  if (previous != nullptr && *previous >= 1 && *previous <= 2) {
    PicoCVInputSetDigitalRole((uint8_t)(*previous - 1), false, pull_up);
  }
  if (selected != 0) {
    PicoCVInputSetDigitalRole((uint8_t)(selected - 1), true, pull_up);
  }
  if (previous != nullptr) *previous = selected;
}

uint16_t PicoCVInputReadRawFresh(uint8_t input) {
  input = clampInput(input);
  const uint16_t raw = acquireRaw(input);
  if (PicoCVInputAnalogAvailable(input)) {
    state.cached_raw[input] = raw;
    state.cached_ms[input] = millis();
    state.cached_ready[input] = true;
  }
  return raw;
}

uint16_t PicoCVInputReadRaw(uint8_t input) {
  input = clampInput(input);
  if (!PicoCVInputAnalogAvailable(input)) {
    return state.cached_ready[input] ? state.cached_raw[input] : 0u;
  }
  const uint32_t now = millis();
  if (!state.cached_ready[input] || state.cached_ms[input] != now) {
    return PicoCVInputReadRawFresh(input);
  }
  return state.cached_raw[input];
}

bool PicoCVInputReadGate(uint8_t input, bool previous_high,
                         uint16_t low_threshold, uint16_t high_threshold) {
  if (!PicoCVInputAnalogAvailable(input)) return false;
  const uint16_t value = (kAdcRange - 1u) - PicoCVInputReadRaw(input);
  return previous_high ? value >= low_threshold : value > high_threshold;
}

bool PicoCVInputReadGate(uint8_t input, bool previous_high) {
  return PicoCVInputReadGate(input, previous_high, kAdcRange / 3u,
                             (kAdcRange * 2u) / 3u);
}

bool PicoCVInputDigitalActiveLow(uint8_t input) {
  input = clampInput(input);
  if (__atomic_load_n(&state.role[input], __ATOMIC_ACQUIRE) !=
      PICOPRO_CV_ROLE_DIGITAL) {
    return false;
  }
  return !digitalRead(PicoCVInputPin(input));
}
