#ifndef PICOPRO_REVERB_MENU_H_
#define PICOPRO_REVERB_MENU_H_

#include "MenuTypes.h"

const char *textoffon[] = {"off", "on"};

void setreverbfeedback_menu(int16_t value, int16_t aux) {
  setreverbfeedback();
}

void setreverblpf_menu(int16_t value, int16_t aux) {
  setreverblpf();
}

struct menu menus[] = {
  // displayed name, min, max, step, type, *textfield, *parameter, aux, *handler
  {"feedback", 0, 100, 1, TYPE_INTEGER, 0, &reverbfeedback, 0, setreverbfeedback_menu},
  {"tone", 550, SAMPLERATE / 2, 500, TYPE_INTEGER, 0, &reverblpf, 0, setreverblpf_menu},
  {"mix", 0, 100, 1, TYPE_INTEGER, 0, &reverbmix, 0, 0},
  {"lvl", 0, 100, 1, TYPE_INTEGER, 0, &reverblevel, 0, 0},
};

#endif // PICOPRO_REVERB_MENU_H_
