#ifndef PICOPRO_RINGS_MENU_H_
#define PICOPRO_RINGS_MENU_H_

#include "MenuTypes.h"

const char *rings_polyphony_names[] = {"1", "2", "4"};
const char *rings_trigger_names[] = {"off", "cv1", "cv2"};
const char *rings_model_names[] = {"modal", "sympat", "string", "fm",
                                   "quant", "reverb"};

struct menu menus[] = {
  {"struct", 0, 1000, 10, TYPE_FLOAT, 0, &rings_structure, 0, updateRingsPatch},
  {"damping", 0, 1000, 10, TYPE_FLOAT, 0, &rings_damping, 0, updateRingsPatch},
  {"bright", 0, 1000, 10, TYPE_FLOAT, 0, &rings_brightness, 0, updateRingsPatch},
  {"freq", 20, 2000, 1, TYPE_INTEGER, 0, &rings_freq, 0, updateRingsPatch},
  {"strum", 0, 2, 1, TYPE_TEXT, rings_trigger_names, &rings_trigger, 0, updateRingsPatch},
  {"position", 0, 1000, 10, TYPE_FLOAT, 0, &rings_position, 0, updateRingsPatch},
  {"slide", 0, 1000, 10, TYPE_FLOAT, 0, &rings_slide, 0, updateRingsPatch},
  {"poly", 0, 2, 1, TYPE_TEXT, rings_polyphony_names, &rings_polyphony, 0, updateRingsPatch},
  {"model", 0, 5, 1, TYPE_TEXT, rings_model_names, &rings_model, 0, updateRingsPatch},
};

#endif
