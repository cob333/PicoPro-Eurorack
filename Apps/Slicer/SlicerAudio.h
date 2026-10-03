#ifndef PICOPRO_SLICER_AUDIO_H
#define PICOPRO_SLICER_AUDIO_H
#include <I2S.h>
#include "SlicerControls.h"

// Bounded nonblocking stream adapter, owning all engine state on core1.
class SlicerAudio {
 public:
  // Startup-only timing configuration; use the installed I2S divider, not just
  // its requested rate. The I2S owner calls this in setup1(), before begin().
  void begin(uint32_t clockHz, uint32_t clocksPerFrame);
  // True if any I/O progressed; false lets the owner await a DMA event.
  bool service(I2S &audio);
 private:
  void serviceClock();
  SlicerEngine engine_;
  SlicerControlPacket controls_;
  int32_t input_[2] = {}, output_[2] = {};
  uint8_t received_ = 0, transmitted_ = 0;
  bool pending_ = false, clockPending_ = false, clockAnchored_ = false;
  uint32_t epoch_ = 0, edgeTimeUs_ = 0, edgeSample_ = 0;
  uint32_t sampleClockHz_ = SlicerEngine::kSampleRate;
  uint64_t timestampDivisor_ = 1000000u, sampleFraction_ = 0;
  uint32_t controlRevision_ = UINT32_MAX;
  float outputGain_ = 0;
  float fadeIncrement_ = 1.0f / 441.0f;
};
#endif
