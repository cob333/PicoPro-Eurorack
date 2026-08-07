#ifndef PICOPRO_FLANGER_MENU_H_
#define PICOPRO_FLANGER_MENU_H_

#include "MenuTypes.h"

const char *flanger_wave_names[] = {
  "triangle", "sine", "square", "saw", "smooth rnd", "step rnd"
};

struct menu menus[] = {
  {"delay", 0, 1000, 10, TYPE_FLOAT, 0, &flanger_delay, 0, updateFlangerMenu},
  {"feedback", 0, 1000, 10, TYPE_FLOAT, 0, &flanger_feedback, 0, updateFlangerMenu},
  {"rate", 0, 1000, 10, TYPE_FLOAT, 0, &flanger_rate, 0, updateFlangerMenu},
  {"depth", 0, 930, 10, TYPE_FLOAT, 0, &flanger_depth, 0, updateFlangerMenu},
  {"mix", 0, 1000, 10, TYPE_FLOAT, 0, &flanger_mix, 0, updateFlangerMenu},
  {"level", 0, 1000, 10, TYPE_FLOAT, 0, &flanger_level, 0, updateFlangerMenu},
  {"width", 0, 1000, 10, TYPE_FLOAT, 0, &flanger_width, 0, updateFlangerMenu},
  {"wave", 0, 5, 1, TYPE_TEXT, flanger_wave_names, &flanger_wave, 0, updateFlangerMenu},
};

#endif
