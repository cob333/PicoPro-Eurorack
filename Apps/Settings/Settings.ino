#include <PicoPro.h>
#include <ClickEncoder.h>
#include "SettingsUI.h"

static ClickEncoder encoder(ENCA_IN, ENCB_IN, ENCSW_IN, ENCDIVIDE);
static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
static SettingsUI ui(display);
static repeating_timer_t encoderTimer;

static bool serviceEncoder(repeating_timer_t *) { encoder.service(); return true; }

void setup() {
  pinMode(ENCSW_IN, INPUT_PULLUP);
  Wire.setSDA(PIN_WIRE_SDA); Wire.setSCL(PIN_WIRE_SCL); Wire.begin();
  add_repeating_timer_us(-TIMER_MICROS, serviceEncoder, nullptr, &encoderTimer);
  PicoCVInputBegin();
  PicoProSystemSettingsBegin();
  ui.begin(display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS), !digitalRead(ENCSW_IN), millis());
  encoder.getValue();
}

void loop() {
  PicoProServiceSelectorExit(ENCSW_IN, encoder);
  const int16_t rotation = encoder.getValue();
  encoder.getButton(); // Drain legacy multiclick events; UI consumes one release.
  ui.service(rotation, !digitalRead(ENCSW_IN), millis());
}
