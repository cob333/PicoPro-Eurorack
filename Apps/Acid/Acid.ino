#include "PicoPro.h"

#include <I2S.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "AcidBass.h"
#include "AcidCV.h"
#include "AcidSequencer.h"
#include "AcidState.h"
#include "AcidUI.h"

namespace {

constexpr uint32_t kSampleRate = 44100u;

}  // namespace

ClickEncoder menuenc(ENCA_IN, ENCB_IN, ENCSW_IN, ENCDIVIDE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
I2S i2s(OUTPUT);

static AcidSequencer sequencer;
static AcidBass bass;
static AcidUI ui(display, menuenc, sequencer, bass);
static volatile bool audio_ready = false;

static void alarmInUsArm(uint32_t delay_us);

static void alarmIrq() {
  menuenc.service();
  hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
  alarmInUsArm(TIMER_MICROS);
}

static void alarmInUsArm(uint32_t delay_us) {
  timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + delay_us;
}

static void startEncoderTimer(uint32_t delay_us) {
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
  irq_set_exclusive_handler(ALARM_IRQ, alarmIrq);
  irq_set_enabled(ALARM_IRQ, true);
  alarmInUsArm(delay_us);
}

static void saveAcidState() {
  AcidSaveState(sequencer, bass);
}

static void prepareAcidExit() {
  bass.prepareExit();
}

void setup() {
  pinMode(ENCA_IN, INPUT_PULLUP);
  pinMode(ENCB_IN, INPUT_PULLUP);
  pinMode(ENCSW_IN, INPUT_PULLUP);
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();
  startEncoderTimer(TIMER_MICROS);
  PicoCVInputBegin();

  i2s.setDOUT(I2S_DATA);
  i2s.setBCLK(BCLK);
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(kSampleRate);
  i2s.begin();

  sequencer.begin();
  bass.begin(kSampleRate);
  AcidCVBegin(bass.config());
  (void)AcidLoadState(sequencer, bass);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    while (true) tight_loop_contents();
  }
  ui.begin();
  audio_ready = true;
}

void loop() {
  PicoProServiceSelectorExit(ENCSW_IN, menuenc, saveAcidState,
                             prepareAcidExit);
  AcidCVServiceModulation(bass);
  ui.service();
}

void setup1() {
  while (!audio_ready) tight_loop_contents();
}

void loop1() {
  const float sample = bass.process(sequencer);
  const int32_t output = (int32_t)(sample * 2147483647.0f);
  i2s.write(output);
  i2s.write(output);
}
