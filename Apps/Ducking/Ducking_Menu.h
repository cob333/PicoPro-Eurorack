#ifndef PICOPRO_DUCKING_MENU_H_
#define PICOPRO_DUCKING_MENU_H_

#include "MenuTypes.h"

const char *ducking_trigger_names[] = {"off", "cv1", "cv2"};

struct menu menus[] = {
  {"atk", 1, 250, 1, TYPE_INTEGER, 0, &ducking_attack, 0, updateDuckingMenu},
  {"dec", 10, 2000, 10, TYPE_INTEGER, 0, &ducking_decay, 0, updateDuckingMenu},
  {"knee", 0, 1000, 10, TYPE_FLOAT, 0, &ducking_knee, 0, updateDuckingMenu},
  {"lvl", 0, 1000, 10, TYPE_FLOAT, 0, &ducking_level, 0, updateDuckingMenu},
  {"trig", 0, 2, 1, TYPE_TEXT, ducking_trigger_names,
   &ducking_trigger, 0, updateDuckingMenu},
};

#endif
