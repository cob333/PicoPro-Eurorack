#ifndef PICOPRO_ADSR_DISPLAY_H_
#define PICOPRO_ADSR_DISPLAY_H_

#include <Arduino.h>
#include "../MenuTypes.h"
#include "CVModulation.h"

enum PicoADSRParamRole {
  PICOPRO_ADSR_PARAM_NONE = 0,
  PICOPRO_ADSR_PARAM_GATE,
  PICOPRO_ADSR_PARAM_ATTACK,
  PICOPRO_ADSR_PARAM_DECAY,
  PICOPRO_ADSR_PARAM_SUSTAIN,
  PICOPRO_ADSR_PARAM_RELEASE
};

struct PicoADSRMenuBinding {
  int16_t *gate;
  int16_t *attack;
  int16_t *decay;
  int16_t *sustain;
  int16_t *release;
};

struct PicoADSRDisplayContext {
  const PicoADSRMenuBinding *binding;
  PicoADSRParamRole role;
  uint8_t attack_index;
  uint8_t decay_index;
  uint8_t sustain_index;
  uint8_t release_index;
};

static const PicoADSRMenuBinding *picoADSRMenuBindings = 0;
static uint8_t picoADSRMenuBindingCount = 0;

static inline void PicoADSRRegisterMenus(const PicoADSRMenuBinding *bindings, uint8_t count) {
  picoADSRMenuBindings = bindings;
  picoADSRMenuBindingCount = count;
}

static inline const PicoADSRMenuBinding *PicoADSRBindingForParameter(int16_t *parameter,
                                                                    PicoADSRParamRole *role) {
  if (role != 0) *role = PICOPRO_ADSR_PARAM_NONE;
  for (uint8_t i = 0; i < picoADSRMenuBindingCount; ++i) {
    const PicoADSRMenuBinding *binding = &picoADSRMenuBindings[i];
    if (parameter == binding->gate) {
      if (role != 0) *role = PICOPRO_ADSR_PARAM_GATE;
      return binding;
    }
    if (parameter == binding->attack) {
      if (role != 0) *role = PICOPRO_ADSR_PARAM_ATTACK;
      return binding;
    }
    if (parameter == binding->decay) {
      if (role != 0) *role = PICOPRO_ADSR_PARAM_DECAY;
      return binding;
    }
    if (parameter == binding->sustain) {
      if (role != 0) *role = PICOPRO_ADSR_PARAM_SUSTAIN;
      return binding;
    }
    if (parameter == binding->release) {
      if (role != 0) *role = PICOPRO_ADSR_PARAM_RELEASE;
      return binding;
    }
  }
  return 0;
}

static inline bool PicoADSRDisplayContextForMenu(const menu *item, PicoADSRDisplayContext *context) {
  PicoADSRParamRole role;
  const PicoADSRMenuBinding *binding = PicoADSRBindingForParameter(item->parameter, &role);
  if (binding == 0 || role == PICOPRO_ADSR_PARAM_NONE) {
    return false;
  }

  context->binding = binding;
  context->role = role;
  context->attack_index = PICOPRO_CV_INVALID_INDEX;
  context->decay_index = PICOPRO_CV_INVALID_INDEX;
  context->sustain_index = PICOPRO_CV_INVALID_INDEX;
  context->release_index = PICOPRO_CV_INVALID_INDEX;

  if (picoCVMenus != 0) {
    for (uint8_t i = 0; i < picoCVMenuCount; ++i) {
      int16_t *parameter = picoCVMenus[i].parameter;
      if (parameter == binding->attack) {
        context->attack_index = i;
      } else if (parameter == binding->decay) {
        context->decay_index = i;
      } else if (parameter == binding->sustain) {
        context->sustain_index = i;
      } else if (parameter == binding->release) {
        context->release_index = i;
      }
    }
  }

  return true;
}

static inline int16_t PicoADSRClampDrawValue(int16_t value, int16_t min_value, int16_t max_value) {
  if (value < min_value) return min_value;
  if (value > max_value) return max_value;
  return value;
}

static inline int16_t PicoADSRDurationWidth(int16_t value, int16_t min_value, int16_t max_value) {
  value = PicoADSRClampDrawValue(value, min_value, max_value);
  if (max_value <= min_value) return 3;
  return 3 + (int16_t)(((int32_t)(value - min_value) * 13) / (max_value - min_value));
}

static inline int16_t PicoADSRSustainY(int16_t sustain) {
  sustain = PicoADSRClampDrawValue(sustain, 0, 100);
  return 17 - (int16_t)(((int32_t)sustain * 11) / 100);
}

static inline void PicoADSRDrawSegmentedLine(int16_t x0,
                                             int16_t y0,
                                             int16_t x1,
                                             int16_t y1,
                                             bool dashed) {
  int16_t dx = abs(x1 - x0);
  int16_t sx = x0 < x1 ? 1 : -1;
  int16_t dy = -abs(y1 - y0);
  int16_t sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;
  uint8_t phase = 0;

  for (;;) {
    if (!dashed || phase < 3) {
      display.drawPixel(x0, y0, WHITE);
    }
    if (x0 == x1 && y0 == y1) break;
    const int16_t e2 = err << 1;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
    if (++phase >= 5) phase = 0;
  }
}

static inline void PicoADSRDrawLine(int16_t x0,
                                    int16_t y0,
                                    int16_t x1,
                                    int16_t y1,
                                    bool highlight,
                                    bool dashed) {
  PicoADSRDrawSegmentedLine(x0, y0, x1, y1, dashed);
  if (highlight) {
    PicoADSRDrawSegmentedLine(x0, y0 - 1, x1, y1 - 1, dashed);
    PicoADSRDrawSegmentedLine(x0, y0 + 1, x1, y1 + 1, dashed);
  }
}

static inline bool PicoADSRShouldRefreshEnvelopeForMenu(const menu *item, uint32_t now_ms) {
  PicoADSRDisplayContext context;
  if (!PicoADSRDisplayContextForMenu(item, &context)) {
    return false;
  }
  const uint8_t indices[] = {
    context.attack_index,
    context.decay_index,
    context.sustain_index,
    context.release_index
  };
  return PicoCVShouldRefreshDisplayGroup(indices, 4, now_ms);
}

static inline bool PicoADSRDrawEnvelopeForMenu(const menu *item) {
  PicoADSRDisplayContext context;
  if (!PicoADSRDisplayContextForMenu(item, &context)) {
    return false;
  }
  const PicoADSRMenuBinding *binding = context.binding;
  const PicoADSRParamRole role = context.role;

  const int16_t base_y = 19;
  const int16_t peak_y = 6;
  const int16_t attack = PicoCVDisplayValueByIndex(context.attack_index, *binding->attack);
  const int16_t decay = PicoCVDisplayValueByIndex(context.decay_index, *binding->decay);
  const int16_t sustain = PicoCVDisplayValueByIndex(context.sustain_index, *binding->sustain);
  const int16_t release = PicoCVDisplayValueByIndex(context.release_index, *binding->release);
  int16_t attack_w = PicoADSRDurationWidth(attack, 0, 100);
  int16_t decay_w = PicoADSRDurationWidth(decay, 0, 100);
  int16_t release_w = PicoADSRDurationWidth(release, 0, 100);
  const int16_t sustain_w = 8;
  const int16_t max_curve_w = 42;
  const int16_t timed_w = attack_w + decay_w + release_w;
  if ((timed_w + sustain_w) > max_curve_w && timed_w > 0) {
    const int16_t available = max_curve_w - sustain_w;
    attack_w = (attack_w * available) / timed_w;
    decay_w = (decay_w * available) / timed_w;
    release_w = available - attack_w - decay_w;
    if (attack_w < 2) attack_w = 2;
    if (decay_w < 2) decay_w = 2;
    if (release_w < 2) release_w = 2;
  }
  const int16_t curve_w = attack_w + decay_w + sustain_w + release_w;
  const int16_t left_x = (64 - curve_w) >> 1;
  const int16_t attack_x = left_x + attack_w;
  const int16_t sustain_x = attack_x + decay_w;
  const int16_t release_x = sustain_x + sustain_w;
  const int16_t end_x = release_x + release_w;
  const int16_t sustain_y = PicoADSRSustainY(sustain);

  const bool dashed = *binding->gate == 0;
  PicoADSRDrawLine(left_x, base_y, attack_x, peak_y,
                   role == PICOPRO_ADSR_PARAM_ATTACK, dashed);
  PicoADSRDrawLine(attack_x, peak_y, sustain_x, sustain_y,
                   role == PICOPRO_ADSR_PARAM_DECAY, dashed);
  PicoADSRDrawLine(sustain_x, sustain_y, release_x, sustain_y,
                   role == PICOPRO_ADSR_PARAM_SUSTAIN, dashed);
  PicoADSRDrawLine(release_x, sustain_y, end_x, base_y,
                   role == PICOPRO_ADSR_PARAM_RELEASE, dashed);
  display.drawPixel(left_x, base_y, WHITE);
  display.drawPixel(attack_x, peak_y, WHITE);
  display.drawPixel(sustain_x, sustain_y, WHITE);
  display.drawPixel(release_x, sustain_y, WHITE);
  display.drawPixel(end_x, base_y, WHITE);
  return true;
}

#endif // PICOPRO_ADSR_DISPLAY_H_
