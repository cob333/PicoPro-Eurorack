#include "SettingsCalibration.h"
#include <CVInput.h>
#include <stdio.h>

void SettingsCalibration::begin() {
  PicoBootLoadCalibration(&calibration_);
  captured_ = 0;
  for (uint8_t cv = 0; cv < 2; ++cv) {
    const float zero = cv ? calibration_.cv2_zero_counts : calibration_.cv1_zero_counts;
    const float cpv = cv ? calibration_.cv2_counts_per_volt : calibration_.cv1_counts_per_volt;
    for (uint8_t v = 0; v < 4; ++v) counts_[cv][v] = zero - cpv * v;
  }
  ready();
}

uint16_t SettingsCalibration::raw(uint8_t step) const {
  return PicoCVInputReadRawFresh(step >= 4 && step < 8 ? 1 : 0);
}

void SettingsCalibration::capture(uint8_t step) {
  if (step >= 8) return;
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 32; ++i) {
    sum += PicoCVInputReadRawFresh(step / 4);
    delay(1);
  }
  counts_[step / 4][step % 4] = (sum + 16) / 32;
  captured_ |= (1u << step);
  snprintf(status_, sizeof(status_), "set c%d %d", step / 4 + 1, step % 4);
}

void SettingsCalibration::fit(uint8_t cv, float &zero, float &cpv) const {
  float sum = 0, weighted = 0;
  for (uint8_t v = 0; v < 4; ++v) {
    sum += counts_[cv][v];
    weighted += v * counts_[cv][v];
  }
  const float slope = (4 * weighted - 6 * sum) / 20;
  zero = (sum - slope * 6) * 0.25f;
  cpv = fmaxf(1, -slope);
}

float SettingsCalibration::countsPerVolt(uint8_t cv) const {
  float zero, cpv;
  fit(cv, zero, cpv);
  return cpv;
}

bool SettingsCalibration::pointsValid(uint8_t cv, float zero, float cpv) const {
  for (uint8_t v = 0; v < 4; ++v) {
    if (v && counts_[cv][v] >= counts_[cv][v - 1]) return false;
    if (fabsf(counts_[cv][v] - (zero - cpv * v)) > 40) return false;
  }
  return true;
}

bool SettingsCalibration::save() {
  if (captured_ != 0xff) { setStatus("need all"); return false; }
  fit(0, calibration_.cv1_zero_counts, calibration_.cv1_counts_per_volt);
  fit(1, calibration_.cv2_zero_counts, calibration_.cv2_counts_per_volt);
  if (!PicoBootCVCalibrationValid(&calibration_) ||
      !pointsValid(0, calibration_.cv1_zero_counts, calibration_.cv1_counts_per_volt) ||
      !pointsValid(1, calibration_.cv2_zero_counts, calibration_.cv2_counts_per_volt)) {
    setStatus("invalid cal");
    return false;
  }
  const bool saved = PicoBootSaveCalibration(&calibration_);
  setStatus(saved ? "saved" : "save err");
  return saved;
}
