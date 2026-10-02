#ifndef PICOPRO_SETTINGS_UI_H
#define PICOPRO_SETTINGS_UI_H
#include <Adafruit_SSD1306.h>
#include "SettingsCalibration.h"
#include "SettingsNavigation.h"

class SettingsUI {
 public:
  explicit SettingsUI(Adafruit_SSD1306 &display) : display_(display) {}
  void begin(bool displayOK, bool buttonDown, uint32_t now);
  void service(int16_t rotation, bool buttonDown, uint32_t now);
 private:
  void draw(uint32_t now);
  void centered(const char *text, int16_t y);
  void drawCalibration();
  void drawPageIndex();
  void drawNavigationArrows();
  Adafruit_SSD1306 &display_;
  SettingsCalibration calibration_;
  SettingsNavigation navigation_;
  SettingsButton button_;
  uint32_t lastDraw_ = 0;
  bool displayOK_ = false, dirty_ = true;
  bool buttonDown_ = false;
  PicoBootSystemSettings settings_{};
  bool saveError_ = false;
};
#endif
