#ifndef PICOPRO_SETTINGS_CALIBRATION_H
#define PICOPRO_SETTINGS_CALIBRATION_H

#include <PicoBootConfig.h>

class SettingsCalibration {
 public:
  void begin();
  void capture(uint8_t step);
  bool save();
  uint16_t raw(uint8_t step) const;
  float countsPerVolt(uint8_t cv) const;
  void ready() { setStatus("ready"); }
  const char *status() const { return status_; }
 private:
  void fit(uint8_t cv, float &zero, float &cpv) const;
  bool pointsValid(uint8_t cv, float zero, float cpv) const;
  void setStatus(const char *text) {
    strncpy(status_, text, sizeof(status_) - 1);
    status_[sizeof(status_) - 1] = 0;
  }
  PicoBootCalibration calibration_{};
  float counts_[2][4]{};
  uint8_t captured_ = 0; // One completion bit per CV/voltage point.
  char status_[14] = "ready";
};
#endif
