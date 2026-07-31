#ifndef PICOPRO_DELAY_MENU_H_
#define PICOPRO_DELAY_MENU_H_

#include "MenuTypes.h"

const char *textoffon[] = {"off", "on"};

struct menu menus[] = {
  // displayed name, min, max, step, type, *textfield, *parameter, aux, *handler
  {"time", 0, 1000, 10, TYPE_INTEGER, 0, &delaytime0, 0, updatepatch},
  {"feedback", 0, 1000, 10, TYPE_FLOAT, 0, &delayfeedback, 0, updatepatch},
  {"x feedback", 0, 1000, 10, TYPE_FLOAT, 0, &crossfeedback, 0, updatepatch},
  {"mix", 0, 1000, 10, TYPE_FLOAT, 0, &wetdrymix, 0, updatepatch},
  {"lvl", 0, 1000, 10, TYPE_FLOAT, 0, &outputlevel, 0, updatepatch},
};

#endif // PICOPRO_DELAY_MENU_H_
