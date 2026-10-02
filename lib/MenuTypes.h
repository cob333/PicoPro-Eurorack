#ifndef PICOPRO_MENU_TYPES_H_
#define PICOPRO_MENU_TYPES_H_

#include <Arduino.h>

enum paramtype {
  TYPE_NONE,
  TYPE_INTEGER,
  TYPE_FLOAT,
  TYPE_TEXT,
  TYPE_SAMPLENAME
};

typedef void (*MenuHandler)(int16_t value, int16_t aux);

struct menu {
  const char *name;
  int16_t min;
  int16_t max;
  int16_t step;
  enum paramtype ptype;
  const char **ptext;
  int16_t *parameter;
  int16_t parameter2;
  MenuHandler handler;
};

#endif // PICOPRO_MENU_TYPES_H_
