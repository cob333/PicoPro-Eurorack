#ifndef PICOPRO_DELAY_OUTPUT_METER_H_
#define PICOPRO_DELAY_OUTPUT_METER_H_

#include <AudioStream.h>
#include <OutputMeter.h>

// Read-only tap of the existing final mixers. Uses references from the fixed
// audio pool, never copies/allocates blocks or changes the output signal.
class DelayOutputMeter : public AudioStream {
 public:
  DelayOutputMeter() : AudioStream(2, inputs_) {}
  void update() override {
    audio_block_t *left = receiveReadOnly(0);
    audio_block_t *right = receiveReadOnly(1);
    for (uint16_t i = 0; i < AUDIO_BLOCK_SAMPLES; ++i)
      PicoOutputMeterObserve16(left ? left->data[i] : 0, right ? right->data[i] : 0);
    if (left) release(left);
    if (right) release(right);
  }
 private:
  audio_block_t *inputs_[2];
};

#endif
