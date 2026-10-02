#ifndef PICOPRO_OUTPUT_METER_H_
#define PICOPRO_OUTPUT_METER_H_

#include <stdint.h>

struct PicoOutputLevels {
  uint16_t left;
  uint16_t right;
};

// One audio producer per application. Observe the final signed 32-bit PCM
// stereo frame after mixing, gain and clipping, immediately before/after I2S.
// No allocation, clock reads, locks, retries or display work on this path.
void PicoOutputMeterObserve(int32_t left, int32_t right);

inline void PicoOutputMeterObserve16(int16_t left, int16_t right) {
  PicoOutputMeterObserve(int32_t(left) * 65536, int32_t(right) * 65536);
}

// UI consumer: both Q16 magnitudes come from one atomic 32-bit snapshot.
PicoOutputLevels PicoOutputMeterRead();

#endif
