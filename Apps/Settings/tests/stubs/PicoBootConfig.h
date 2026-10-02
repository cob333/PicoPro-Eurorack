#ifndef SETTINGS_TEST_BOOT_CONFIG_H
#define SETTINGS_TEST_BOOT_CONFIG_H
#include <stdint.h>
#include <cstring>
#include <cmath>
#include "../../../../lib/PicoProlib/PicoBootSystemSettings.h"
struct PicoBootCalibration {
  float cv1_counts_per_volt, cv2_counts_per_volt, cvout_counts_per_volt;
  float cv1_zero_counts, cv2_zero_counts, cvout_zero_counts, reserved[2];
};
int PicoBootLoadCalibration(PicoBootCalibration *);
int PicoBootCVCalibrationValid(const PicoBootCalibration *);
int PicoBootSaveCalibration(const PicoBootCalibration *);
void delay(uint32_t);
int PicoBootLoadSystemSettings(PicoBootSystemSettings *);
int PicoBootSaveSystemSettings(const PicoBootSystemSettings *);
#endif
