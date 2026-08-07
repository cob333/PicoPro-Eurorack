#ifndef PICOPRO_GLITCH_MENU_H_
#define PICOPRO_GLITCH_MENU_H_

#include "MenuTypes.h"

const char *glitch_clock_names[] = {"internal", "cv1", "cv2"};
const char *glitch_clock_ratio_names[] = {"/2", "x1", "x2", "x3", "x4"};

struct menu menus[] = {
  {"break", 0, 1000, 10, TYPE_FLOAT, 0, &glitch_break, 0, updateGlitchMenu},
  {"reverse", 0, 1000, 10, TYPE_FLOAT, 0, &glitch_reverse, 0, updateGlitchMenu},
  {"repeat", 0, 1000, 10, TYPE_FLOAT, 0, &glitch_repeat, 0, updateGlitchMenu},
  {"lvl", 0, 1000, 10, TYPE_FLOAT, 0, &glitch_level, 0, updateGlitchMenu},
  {"clk", 0, 2, 1, TYPE_TEXT, glitch_clock_names, &glitch_clock, 0, updateGlitchMenu},
  {"bpm", 20, 200, 1, TYPE_INTEGER, 0, &glitch_bpm, 0, updateGlitchMenu},
};

#endif
