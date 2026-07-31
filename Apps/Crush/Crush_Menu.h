#ifndef PICOPRO_CRUSH_MENU_H_
#define PICOPRO_CRUSH_MENU_H_

#include "MenuTypes.h"

struct menu menus[] = {
  {"bits", 1, 16, 1, TYPE_INTEGER, 0, &crush_bits, 0, updateCrushMenu},
  {"sample", 1, 20, 1, TYPE_INTEGER, 0, &crush_downsample, 0, updateCrushMenu},
  {"mix", 0, 1000, 10, TYPE_FLOAT, 0, &crush_mix, 0, updateCrushMenu},
  {"lvl", 0, 1000, 10, TYPE_FLOAT, 0, &crush_level, 0, updateCrushMenu},
};

#endif
