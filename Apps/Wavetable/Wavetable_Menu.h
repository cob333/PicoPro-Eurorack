#ifndef PICOPRO_WAVETABLE_MENU_H_
#define PICOPRO_WAVETABLE_MENU_H_

#include "MenuTypes.h"

const char *textgate[] = {"off", "cv1", "cv2"};

struct menu menus[] = {
  // displayed name, min, max, step, type, *textfield, *parameter, aux, *handler
  {"bank", 0, PICOPRO_WAVETABLE_BANK_COUNT - 1, 1, TYPE_TEXT,
   PICOPRO_WAVETABLE_BANK_NAMES, &wavetablebank, 0, updateWavetableMenu},
  {"wave", 0, PICOPRO_WAVETABLE_COUNT - 1, 1, TYPE_INTEGER, 0, &wavetablewave, 0, updateWavetableMenu},
  {"freq", 20, 2000, 1, TYPE_INTEGER, 0, &wavetablefreq, 0, updateWavetableMenu},
  {"level", 0, 100, 1, TYPE_INTEGER, 0, &wavetablelevel, 0, updateWavetableMenu},
  {"gate", 0, 2, 1, TYPE_TEXT, textgate, &wavetable_adsr_gate, 0, updateWavetableMenu},
  {"atk", 0, 100, 1, TYPE_INTEGER, 0, &wavetable_adsr_attack, 0, updateWavetableMenu},
  {"dec", 0, 100, 1, TYPE_INTEGER, 0, &wavetable_adsr_decay, 0, updateWavetableMenu},
  {"sus", 0, 100, 1, TYPE_INTEGER, 0, &wavetable_adsr_sustain, 0, updateWavetableMenu},
  {"rel", 0, 100, 1, TYPE_INTEGER, 0, &wavetable_adsr_release, 0, updateWavetableMenu},
};

#endif // PICOPRO_WAVETABLE_MENU_H_
