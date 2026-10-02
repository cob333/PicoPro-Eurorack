#ifndef PICOPRO_TUNER_DETECTOR_H
#define PICOPRO_TUNER_DETECTOR_H
#include <stdint.h>
struct TunerReading {
  float frequency = 0;
  bool valid = false;
};
class TunerDetector {
 public:
  static constexpr uint32_t kRate = 22050;
  static constexpr uint16_t kFrame = 4096;
  static constexpr uint16_t kMaxLag = 692;
  static TunerReading analyze(const float *samples);
};
#endif
