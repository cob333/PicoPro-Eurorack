#ifndef PICOPRO_GRAIN_ENGINE_TYPES_H_
#define PICOPRO_GRAIN_ENGINE_TYPES_H_

#include <stdint.h>

struct GrainVoice {
  uint64_t phase_q16;
  uint32_t age;
  uint32_t duration;
  uint32_t region_start;
  uint32_t region_frames;
  uint32_t phase_inc_q16;
  uint8_t shape;
  bool active;
};

#endif
