#ifndef SETTINGS_TEST_DISPLAY_H
#define SETTINGS_TEST_DISPLAY_H
#include "Adafruit_GFX.h"
#include <string>
#include <cstring>
#define WHITE 1
#define BLACK 0
class Adafruit_SSD1306 {
 public:
  std::string text;
  unsigned rotation = 0, updates = 0;
  void invertDisplay(bool) {}
  void setRotation(unsigned r) { rotation = r; }
  void clearDisplay() { text.clear(); }
  void setFont(const GFXfont *) {}
  void setTextSize(unsigned) {}
  void setTextColor(unsigned, unsigned = 0) {}
  void setCursor(int, int) {}
  void drawRect(int, int, int, int, int) {}
  void fillRect(int, int, int, int, int) {}
  void drawLine(int, int, int, int, int) {}
  void getTextBounds(const char *s, int, int, int16_t *x, int16_t *y, uint16_t *w, uint16_t *h) {
    *x = *y = 0; *w = strlen(s)*4; *h = 6;
  }
  void print(const char *s) { text += s; }
  void write(uint8_t c) { text += (char)c; }
  void display() { ++updates; }
};
#endif
