#include "SlicerAudio.h"
#include <CVInput.h>
#include <PicoSystemRuntime.h>
#include <OutputMeter.h>
#include <math.h>
#include <limits.h>

SlicerControls slicerControls;

static int32_t toPCM(float value) {
  if (!isfinite(value)) return 0;
  if (value >= 1.0f) return INT32_MAX;
  if (value <= -1.0f) return INT32_MIN;
  return static_cast<int32_t>(value * 2147483648.0f);
}

void SlicerAudio::begin(uint32_t clockHz, uint32_t clocksPerFrame) {
  if (!clocksPerFrame) return;
  const float rate = float(clockHz) / clocksPerFrame;
  if (rate < 8000.0f || rate > 192000.0f) return;
  if (!engine_.configureSampleRate(rate)) return;
  sampleClockHz_ = clockHz;
  timestampDivisor_ = uint64_t(clocksPerFrame) * 1000000u;
  sampleFraction_ = 0;
  fadeIncrement_ = 1.0f / (rate * 0.010f);
}

void SlicerAudio::serviceClock() {
  if (epoch_ != controls_.clockEpoch) {
    epoch_ = controls_.clockEpoch;
    clockPending_ = clockAnchored_ = false;
    sampleFraction_ = 0;
    engine_.resetClockTracking();
  }
  // Clock intervals use IRQ timestamps, never OLED/control polling intervals.
  // Anchor the first edge to this stream; later edges preserve sub-sample carry.
  for (uint8_t i = 0; i < 16; ++i) {
    if (clockPending_) {
      if (static_cast<int32_t>(engine_.sampleTime() - edgeSample_) < 0) return;
      engine_.clockEdge(edgeSample_);
      clockPending_ = false;
    }
    PicoCVClockEdge edge;
    if (!PicoCVInputPopClockEdge(&edge)) return;
    if (edge.epoch != epoch_ || controls_.parameters.clock == SlicerClock::Internal) continue;
    if (!clockAnchored_) {
      edgeSample_ = engine_.sampleTime();
      clockAnchored_ = true;
    } else {
      const uint32_t interval = edge.time_us - edgeTimeUs_;
      if (interval < 1000u) continue; // Reject sub-ms input glitches.
      if (interval > SlicerEngine::kClockTimeoutMs * 1000u) {
        edgeSample_ = engine_.sampleTime(); sampleFraction_ = 0;
      } else {
        const uint64_t samples = uint64_t(interval) * sampleClockHz_ + sampleFraction_;
        edgeSample_ += samples / timestampDivisor_;
        sampleFraction_ = samples % timestampDivisor_;
      }
    }
    edgeTimeUs_ = edge.time_us;
    clockPending_ = true;
  }
}

bool SlicerAudio::service(I2S &audio) {
  bool progressed = false;
  if (slicerControls.read(controls_, controlRevision_)) engine_.setParameters(controls_.parameters);
  for (uint8_t count = 0; count < 32; ++count) {
    if (!pending_) {
      if (received_ < sizeof(input_)) {
        if (audio.available() < 4) break;
        const size_t bytes = audio.read(reinterpret_cast<uint8_t *>(input_) + received_, sizeof(input_) - received_);
        if (!bytes) break; // No progress: yield instead of retrying within this service call.
        progressed = true;
        received_ += bytes;
        if (received_ != sizeof(input_)) continue;
      }
      serviceClock();
      PicoProRouteAudioInput(input_[0], input_[1]);
      float left, right;
      engine_.process(input_[0] * (1.0f / 2147483648.0f),
                      input_[1] * (1.0f / 2147483648.0f), left, right);
      const float target = slicerControls.muted() ? 0.0f : 1.0f;
      outputGain_ += fmaxf(-fadeIncrement_, fminf(fadeIncrement_, target - outputGain_));
      output_[0] = toPCM(left * outputGain_);
      output_[1] = toPCM(right * outputGain_);
      pending_ = true;
    }
    const size_t bytes = audio.write(reinterpret_cast<const uint8_t *>(output_) + transmitted_,
                                     sizeof(output_) - transmitted_);
    progressed = progressed || bytes != 0;
    transmitted_ += bytes;
    if (transmitted_ != sizeof(output_)) break;
    PicoOutputMeterObserve(output_[0], output_[1]); // Exactly once per emitted frame.
    received_ = transmitted_ = 0;
    pending_ = false;
  }
  return progressed;
}
