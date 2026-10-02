#ifndef PICOPRO_CV_PERSISTENCE_H_
#define PICOPRO_CV_PERSISTENCE_H_

#include <stdint.h>

#ifndef PICOPRO_CV_MAX_MENUS
#define PICOPRO_CV_MAX_MENUS 16
#endif

#define PICOPRO_CV_LEGACY_STATE_VERSION 1u
#define PICOPRO_CV_STATE_VERSION 2u

typedef struct {
  uint8_t input;
  int8_t amount;
} PicoCVPersistentAssignment;

typedef struct {
  uint8_t version;
  uint8_t count;
  PicoCVPersistentAssignment assignments[PICOPRO_CV_MAX_MENUS];
} PicoCVPersistentState;

#endif // PICOPRO_CV_PERSISTENCE_H_
