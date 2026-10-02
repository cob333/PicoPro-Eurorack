#include "TunerDetector.h"
#include <math.h>
TunerReading TunerDetector::analyze(const float *samples) {
  TunerReading result;
  float energy = 0;
  for (uint16_t i = 0; i < kFrame; ++i) energy += samples[i] * samples[i];
  // RMS floor 0.0002 (previously 0.0001): reject more idle-input noise.
  // Compare squared RMS, avoiding a square root in every window.
  if (energy < kFrame * 4e-8f) return result;
  float sum = 0;
  float before = 1, previous = 1;
  float raw_before = 0, raw_previous = 0;
  float a = 0, b = 0, c = 0, confidence = 0;
  uint16_t candidate = 0;
  for (uint16_t lag = 1; lag <= kMaxLag; ++lag) {
    float value = 0;
    for (uint16_t i = 0; i < kFrame / 2; ++i) {
      const float delta = samples[i] - samples[i + lag];
      value += delta * delta;
    }
    sum += value;
    const float normalized = sum > 1e-20f ? value * lag / sum : 1;
    // Same first local minimum as the full CMND table. Once its right
    // neighbor is known, later lags cannot change the chosen candidate.
    if (lag >= 11 && previous < 0.15f && previous <= before && previous < normalized) {
      candidate = lag - 1;
      confidence = 1 - previous;
      a = raw_before; b = raw_previous; c = value;
      break;
    }
    before = previous; previous = normalized;
    raw_before = raw_previous; raw_previous = value;
  }
  if (!candidate) return result;
  // Reuse the three raw differences already computed above.
  const float denominator = a - 2 * b + c;
  float offset = fabsf(denominator) > 1e-12f ? 0.5f * (a - c) / denominator : 0;
  if (offset < -0.5f) offset = -0.5f;
  if (offset > 0.5f) offset = 0.5f;
  result.frequency = kRate / (candidate + offset);
  result.confidence = confidence;
  result.valid = result.frequency >= 31.9f && result.frequency <= 2010.0f;
  return result;
}
