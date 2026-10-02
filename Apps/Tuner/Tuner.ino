#include "PicoPro.h"
#include <I2S.h>
#include <Adafruit_SSD1306.h>
#include "TunerAudio.h"
#include "TunerDetector.h"
#include "TunerUI.h"

static I2S audio(INPUT_PULLUP);
static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
static TunerAudio capture;
static TunerUI ui(display);
static bool ready = false;

void setup() {
  pinMode(ENCSW_IN, INPUT_PULLUP);
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();
  audio.setDIN(I2S_DATAIN);
  audio.setDOUT(I2S_DATA);
  audio.setBCLK(BCLK);
  audio.setMCLK(MCLK);
  audio.setMCLKmult(256);
  audio.setBitsPerSample(32);
  audio.setFrequency(TunerAudio::kInputRate);
  audio.setBuffers(8, 256);
  const bool audio_ok = audio.begin();
  ui.begin(display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS), audio_ok);
  __atomic_store_n(&ready, audio_ok, __ATOMIC_RELEASE);
}

void loop() {
  PicoProServiceSelectorExit(ENCSW_IN);
  const float *frame = capture.acquire();
  if (frame) {
    ui.update(TunerDetector::analyze(frame), millis());
    capture.release(frame);
  }
  ui.service(millis());
}

void setup1() {}
void loop1() {
  if (__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) capture.service(audio);
}
