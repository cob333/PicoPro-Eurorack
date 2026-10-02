#include "SlicerOctave.h"
#include <math.h>

namespace {
constexpr uint32_t kMask = SlicerOctave::kBufferFrames - 1u;
constexpr float kMinimumDelay = 16.0f;
static_assert(SlicerOctave::kBufferFrames == 2u * SlicerOctave::kWindowFrames,
              "Octave phase derives from the power-of-two history cursor");
static_assert((SlicerOctave::kBufferFrames & kMask) == 0,
              "History must be a power of two");

float window(float phase) {
  const float triangle = phase < 0.5f ? 2.0f * phase : 2.0f * (1.0f - phase);
  return triangle * triangle * (3.0f - 2.0f * triangle);
}
float filter(float value, float input) {
  const float next = value + 0.5f * (input - value);
  return fabsf(next) < 1e-20f ? 0.0f : next; // No persistent denormal silence tail.
}
}

void SlicerOctave::push(float left, float right) {
  write_ = (write_ + 1u) & kMask;
  const float input[] = {left, right};
  for (uint8_t channel = 0; channel < 2; ++channel) {
    // Two cheap poles soften high-frequency octave-up aliasing. This is not
    // a brick-wall anti-alias filter; unshifted and dry audio are unaffected.
    lowpass_[channel][0] = filter(lowpass_[channel][0], input[channel]);
    lowpass_[channel][1] = filter(lowpass_[channel][1], lowpass_[channel][0]);
    history_[channel][write_] = lowpass_[channel][1];
  }
}

float SlicerOctave::tap(uint8_t channel, float delay) const {
  const uint32_t whole = static_cast<uint32_t>(delay);
  const float fraction = delay - whole;
  const uint32_t first = (write_ - whole) & kMask;
  const float a = history_[channel][first];
  return a + fraction * (history_[channel][(first - 1u) & kMask] - a);
}

void SlicerOctave::voice(bool up, float &left, float &right) const {
  const uint32_t period = up ? kWindowFrames : 2u * kWindowFrames;
  const float phase = float(write_ & (period - 1u)) *
                      (up ? 1.0f / kWindowFrames : 1.0f / (2u * kWindowFrames));
  const float second = phase < 0.5f ? phase + 0.5f : phase - 0.5f;
  const float weight = window(phase);
  // d(delay)/dn = -1 for octave up and +0.5 for octave down: read speeds
  // are respectively 2 and 0.5. Each wrapping head has zero window weight.
  const float first_delay = kMinimumDelay + kWindowFrames * (up ? 1.0f - phase : phase);
  const float second_delay = kMinimumDelay + kWindowFrames * (up ? 1.0f - second : second);
  left = weight * tap(0, first_delay) + (1.0f - weight) * tap(0, second_delay);
  right = weight * tap(1, first_delay) + (1.0f - weight) * tap(1, second_delay);
}

void SlicerOctave::blend(float up, float down, float left, float right,
                         float &out_left, float &out_right) const {
  out_left = left * (1.0f - up - down);
  out_right = right * (1.0f - up - down);
  float shifted_left, shifted_right;
  if (up > 0.0f) {
    voice(true, shifted_left, shifted_right);
    out_left += up * shifted_left; out_right += up * shifted_right;
  }
  if (down > 0.0f) {
    voice(false, shifted_left, shifted_right);
    out_left += down * shifted_left; out_right += down * shifted_right;
  }
}
