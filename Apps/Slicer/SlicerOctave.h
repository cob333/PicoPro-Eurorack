#ifndef PICOPRO_SLICER_OCTAVE_H
#define PICOPRO_SLICER_OCTAVE_H

#include <stdint.h>

// Stereo dual-read-head octave shifter. One shared fixed history supports both
// directions; no allocation, feedback, buffer clearing or trig in processing.
class SlicerOctave {
 public:
  static constexpr uint32_t kBufferFrames = 2048;
  static constexpr uint32_t kWindowFrames = 1024;
  void push(float left, float right);
  void blend(float up, float down, float left, float right,
             float &out_left, float &out_right) const;

 private:
  float tap(uint8_t channel, float delay) const;
  void voice(bool up, float &left, float &right) const;
  float history_[2][kBufferFrames] = {};
  float lowpass_[2][2] = {};
  uint32_t write_ = kBufferFrames - 1u;
};

#endif
