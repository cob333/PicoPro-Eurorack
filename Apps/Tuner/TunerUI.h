#ifndef PICOPRO_TUNER_UI_H
#define PICOPRO_TUNER_UI_H
#include <Adafruit_SSD1306.h>
#include "TunerDetector.h"
class TunerUI {
 public:
  explicit TunerUI(Adafruit_SSD1306 &display) : display_(display) {}
  void begin(bool display_ok, bool audio_ok);
  void update(const TunerReading &reading, uint32_t now);
  void service(uint32_t now);
 private:
  bool draw();
  Adafruit_SSD1306 &display_;
  TunerReading reading_;
  bool available_ = false, audio_ok_ = false, dirty_ = true;
  bool inverted_ = false;
  uint32_t updated_ms_ = 0, drawn_ms_ = 0;
  float smooth_frequency_ = 0;
  int16_t last_note_ = -2, last_cents_ = 0, last_hz_ = 0;
};
#endif
