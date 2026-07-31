#ifndef PICOPRO_GRAIN_MENU_H_
#define PICOPRO_GRAIN_MENU_H_

#include "MenuTypes.h"

const char *grain_sample_names[] = {"downer", "guitar"};
const char *grain_shape_names[] = {"rect", "triangle", "hann", "tukey"};
const char *grain_gate_names[] = {"off", "cv1", "cv2"};

struct menu menus[] = {
  {"sample", 0, PICOPRO_GRAIN_SAMPLE_COUNT - 1, 1, TYPE_TEXT,
   grain_sample_names, &grain_sample, 0, updateGrainMenu},
  {"freq", 20, 2000, 1, TYPE_INTEGER, 0, &grain_freq, 0, updateGrainMenu},
  {"size", 5, 500, 1, TYPE_INTEGER, 0, &grain_size, 0, updateGrainMenu},
  {"shape", 0, 3, 1, TYPE_TEXT,
   grain_shape_names, &grain_shape, 0, updateGrainMenu},
  {"len", 1, 1000, 10, TYPE_FLOAT, 0, &grain_length, 0, updateGrainMenu},
  {"pos", 0, 1000, 10, TYPE_FLOAT, 0, &grain_position, 0, updateGrainMenu},
  {"gate", 0, 2, 1, TYPE_TEXT,
   grain_gate_names, &grain_adsr_gate, 0, updateGrainMenu},
  {"atk", 0, 100, 1, TYPE_INTEGER, 0, &grain_adsr_attack, 0, updateGrainMenu},
  {"dec", 0, 100, 1, TYPE_INTEGER, 0, &grain_adsr_decay, 0, updateGrainMenu},
  {"sus", 0, 100, 1, TYPE_INTEGER, 0, &grain_adsr_sustain, 0, updateGrainMenu},
  {"rel", 0, 100, 1, TYPE_INTEGER, 0, &grain_adsr_release, 0, updateGrainMenu},
};

#endif
