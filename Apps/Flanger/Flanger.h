// DaisySP delay-line subset used by the PicoPro Flanger.
#ifndef PICOPRO_FLANGER_DELAY_H_
#define PICOPRO_FLANGER_DELAY_H_

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

namespace daisysp {

inline float fclamp(float value, float minimum, float maximum) {
  return fminf(fmaxf(value, minimum), maximum);
}

template <typename T, size_t max_size>
class DelayLine {
 public:
  void Init() { Reset(); }

  void Reset() {
    for (size_t i = 0; i < max_size; ++i) line_[i] = T(0);
    write_ptr_ = 0;
    delay_ = 1;
    frac_ = 0.0f;
  }

  inline void SetDelay(float delay) {
    int32_t integral = static_cast<int32_t>(delay);
    if (integral < 0) integral = 0;
    if (integral >= static_cast<int32_t>(max_size)) integral = max_size - 1;
    delay_ = static_cast<size_t>(integral);
    frac_ = delay - static_cast<float>(integral);
    if (frac_ < 0.0f) frac_ = 0.0f;
  }

  inline void Write(T sample) {
    line_[write_ptr_] = sample;
    write_ptr_ = (write_ptr_ + max_size - 1) % max_size;
  }

  inline T Read() const {
    const T a = line_[(write_ptr_ + delay_) % max_size];
    const T b = line_[(write_ptr_ + delay_ + 1) % max_size];
    return a + (b - a) * frac_;
  }

 private:
  float frac_ = 0.0f;
  size_t write_ptr_ = 0;
  size_t delay_ = 1;
  T line_[max_size];
};

}  // namespace daisysp
#endif
