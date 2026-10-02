#ifndef PICOPRO_TUNER_FRONTEND_H
#define PICOPRO_TUNER_FRONTEND_H
#include <stdint.h>
// Input normalization is performed by the I2S owner. This stateful frontend is
// independent of Arduino and only performs capture filtering and decimation.
class TunerFrontend {
 public:
  bool process(float input, float *output) {
    dc_ += 0.0007121f * (input - dc_);
    float value = input - dc_;
    for (uint8_t pole = 0; pole < 4; ++pole) {
      lowpass_[pole] += 0.3478f * (value - lowpass_[pole]);
      value = lowpass_[pole];
    }
    if (++divider_ < 2) return false;
    divider_ = 0;
    *output = value;
    return true;
  }
 private:
  float lowpass_[4] = {}, dc_ = 0;
  uint8_t divider_ = 0;
};
#endif
