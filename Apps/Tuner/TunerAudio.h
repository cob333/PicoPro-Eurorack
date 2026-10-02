#ifndef PICOPRO_TUNER_AUDIO_H
#define PICOPRO_TUNER_AUDIO_H
#include <I2S.h>
#include "TunerDetector.h"
#include "TunerFrontend.h"
class TunerAudio {
 public:
  static constexpr uint32_t kInputRate = 44100;
  static constexpr uint16_t kHop = 1024; // 46.4 ms at the analysis rate.
  void service(I2S &audio);
  const float *acquire();
  void release(const float *frame);
  uint32_t droppedWindows() const { return __atomic_load_n(&dropped_, __ATOMIC_RELAXED); }
 private:
  enum : uint8_t { FREE, FILL, READY, READING };
  float frames_[3][TunerDetector::kFrame] = {};
  float history_[TunerDetector::kFrame] = {};
  uint8_t state_[3] = {FREE, FREE, FREE};
  uint16_t index_ = 0;
  uint16_t collected_ = 0, hop_ = 0;
  TunerFrontend frontend_;
  int32_t pair_[2] = {};
  uint8_t received_ = 0;
  uint8_t transmitted_ = 0;
  uint32_t dropped_ = 0;
  void publish();
};
#endif
