// Centralized PicoPro CV input acquisition and digital-role management.

#ifndef PICOPRO_CV_INPUT_H_
#define PICOPRO_CV_INPUT_H_

#include <Arduino.h>

enum PicoCVInputRole : uint8_t {
  PICOPRO_CV_ROLE_ANALOG = 0,
  PICOPRO_CV_ROLE_DIGITAL = 1,
};

void PicoCVInputBegin(void);
uint8_t PicoCVInputPin(uint8_t input);
bool PicoCVInputAnalogAvailable(uint8_t input);
void PicoCVInputSetDigitalRole(uint8_t input, bool enabled,
                               bool pull_up = true);
void PicoCVInputSelectDigitalRole(int8_t one_based_input, int8_t *previous,
                                  bool pull_up = true);
uint16_t PicoCVInputReadRawFresh(uint8_t input);
uint16_t PicoCVInputReadRaw(uint8_t input);
bool PicoCVInputReadGate(uint8_t input, bool previous_high,
                         uint16_t low_threshold, uint16_t high_threshold);
bool PicoCVInputReadGate(uint8_t input, bool previous_high);
bool PicoCVInputDigitalActiveLow(uint8_t input);

#endif  // PICOPRO_CV_INPUT_H_
