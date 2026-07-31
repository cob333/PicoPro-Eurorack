#ifndef PICOPRO_PLACEHOLDER_APP_H_
#define PICOPRO_PLACEHOLDER_APP_H_

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

#include "PicoPro.h"

static Adafruit_SSD1306 placeholder_display(
    SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

static inline void PicoProPlaceholderSetup(const char *name) {
  (void)name;
  pinMode(ENCSW_IN, INPUT_PULLUP);
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();

  if (!placeholder_display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    return;
  }

  placeholder_display.clearDisplay();
  placeholder_display.setTextColor(SSD1306_WHITE);
  placeholder_display.setTextSize(1);
  placeholder_display.setCursor(14, 8);
  placeholder_display.print("coming");
  placeholder_display.setCursor(11, 16);
  placeholder_display.print("soon...");
  placeholder_display.display();
}

static inline void PicoProPlaceholderLoop(void) {
  PicoProServiceSelectorExit(ENCSW_IN);
  delay(1);
}

#endif  // PICOPRO_PLACEHOLDER_APP_H_
