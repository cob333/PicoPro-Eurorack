#include "OutputMeter.h"

static_assert(__atomic_always_lock_free(sizeof(uint32_t), nullptr),
              "Output meter snapshots must not use runtime locks");

namespace {
uint32_t published = 0;
uint16_t peakLeft = 0, peakRight = 0;
uint16_t heldLeft = 0, heldRight = 0;
uint8_t divider = 0;

uint16_t magnitude(int32_t sample) {
  // Unsigned negation is defined even for INT32_MIN.
  const uint32_t bits = static_cast<uint32_t>(sample);
  const uint32_t value = (sample < 0 ? 0u - bits : bits) >> 15;
  return value > 65535u ? 65535u : static_cast<uint16_t>(value);
}

uint16_t release(uint16_t held, uint16_t peak) {
  // Fast attack, ~0.5 s fall to -48 dB at 44.1 kHz. Round decay up so silence
  // reaches zero. Only calculated once per 256 stereo frames, not per sample.
  held -= (uint32_t(held) + 15u) >> 4;
  return peak > held ? peak : held;
}
}  // namespace

void PicoOutputMeterObserve(int32_t left, int32_t right) {
  const uint16_t l = magnitude(left), r = magnitude(right);
  if (l > peakLeft) peakLeft = l;
  if (r > peakRight) peakRight = r;
  if (++divider != 0) return;
  heldLeft = release(heldLeft, peakLeft);
  heldRight = release(heldRight, peakRight);
  peakLeft = peakRight = 0;
  __atomic_store_n(&published, uint32_t(heldLeft) | (uint32_t(heldRight) << 16),
                   __ATOMIC_RELEASE);
}

PicoOutputLevels PicoOutputMeterRead() {
  const uint32_t value = __atomic_load_n(&published, __ATOMIC_ACQUIRE);
  return {static_cast<uint16_t>(value), static_cast<uint16_t>(value >> 16)};
}
