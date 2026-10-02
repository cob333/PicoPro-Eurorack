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

constexpr uint8_t kClockQueueSize = 16;
static_assert((kClockQueueSize & (kClockQueueSize - 1)) == 0,
              "Clock queue size must be a power of two");
PicoCVClockEdge clockQueue[kClockQueueSize];
uint32_t clockHead = 0, clockTail = 0, clockEpoch = 0;
int8_t clockInput = 0;

void captureClock(uint8_t input) {
  if (__atomic_load_n(&clockInput, __ATOMIC_ACQUIRE) != input + 1 ||
      PicoCVInputAnalogAvailable(input)) return;
  const uint32_t head = __atomic_load_n(&clockHead, __ATOMIC_RELAXED);
  const uint32_t tail = __atomic_load_n(&clockTail, __ATOMIC_ACQUIRE);
  if (head - tail >= kClockQueueSize) return;
  clockQueue[head & (kClockQueueSize - 1)] =
      {micros(), __atomic_load_n(&clockEpoch, __ATOMIC_ACQUIRE)};
  __atomic_store_n(&clockHead, head + 1, __ATOMIC_RELEASE);
}
void captureClock1() { captureClock(0); }
void captureClock2() { captureClock(1); }

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

void PicoCVInputSelectClockCapture(int8_t one_based_input) {
  const int8_t next = one_based_input >= 1 && one_based_input <= 2 ? one_based_input : 0;
  const int8_t previous = __atomic_load_n(&clockInput, __ATOMIC_ACQUIRE);
  if (next == previous) return;
  if (previous) {
    const uint8_t old = previous - 1;
    detachInterrupt(digitalPinToInterrupt(PicoCVInputPin(old)));
    PicoCVInputSetDigitalRole(old, false);
  }
  __atomic_store_n(&clockInput, next, __ATOMIC_RELEASE);
  __atomic_add_fetch(&clockEpoch, 1u, __ATOMIC_RELEASE);
  if (next) {
    const uint8_t input = next - 1;
    PicoCVInputSetDigitalRole(input, true);
    attachInterrupt(digitalPinToInterrupt(PicoCVInputPin(input)),
                    input ? captureClock2 : captureClock1, FALLING);
  }
}

uint32_t PicoCVInputClockCaptureEpoch() {
  return __atomic_load_n(&clockEpoch, __ATOMIC_ACQUIRE);
}

bool PicoCVInputPopClockEdge(PicoCVClockEdge *edge) {
  if (!edge) return false;
  const uint32_t tail = __atomic_load_n(&clockTail, __ATOMIC_RELAXED);
  if (tail == __atomic_load_n(&clockHead, __ATOMIC_ACQUIRE)) return false;
  *edge = clockQueue[tail & (kClockQueueSize - 1)];
  __atomic_store_n(&clockTail, tail + 1, __ATOMIC_RELEASE);
  return true;
}
