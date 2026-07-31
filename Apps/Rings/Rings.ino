// Mutable Instruments Rings for PicoPro.
// Trigger source is selectable between CV1 and CV2. Frequency uses the shared
// PicoPro CV assignment UI and defaults to CV2 at 1 V/oct.

#include "PicoPro.h"
#include <I2S.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ClickEncoder.h"
#include "PicoAppState.h"

#define SAMPLERATE 44100
#include "Rings.h"

enum UISTATES { RUN, DORMANT, WAIT_BUTTON_RELEASE };
int16_t UI_state = RUN;
#define DEBOUNCE_CYCLES 100

ClickEncoder menuenc(ENCA_IN, ENCB_IN, ENCSW_IN, ENCDIVIDE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
I2S rings_dac(OUTPUT);
int32_t displaytimer;

#include "ui/CVModulation.h"

int16_t rings_structure = 250;
int16_t rings_damping = 550;
int16_t rings_brightness = 550;
int16_t rings_freq = 130;
int16_t rings_trigger = 1;
int16_t rings_position = 250;
int16_t rings_slide = 0;
int16_t rings_polyphony = 0;
int16_t rings_model = 0;

static volatile bool rings_audio_ready = false;
static bool trigger_high = false;
static float smoothed_cv_note = 0.0f;
static uint32_t pitch_update_us = 0;
static bool pitch_initialized = false;

#define RINGS_STATE_TAG PICOPRO_APP_STATE_TAG('R', 'N', 'G', '3')
#define RINGS_V2_STATE_TAG PICOPRO_APP_STATE_TAG('R', 'N', 'G', '2')
#define RINGS_V1_STATE_TAG PICOPRO_APP_STATE_TAG('R', 'N', 'G', '1')
struct RingsState {
  int16_t structure;
  int16_t damping;
  int16_t brightness;
  int16_t freq;
  int16_t trigger;
  int16_t position;
  int16_t slide;
  int16_t polyphony;
  int16_t model;
  PicoCVPersistentState cv;
};

struct RingsV2State {
  int16_t structure;
  int16_t damping;
  int16_t brightness;
  int16_t note;
  int16_t trigger;
  int16_t position;
  int16_t slide;
  int16_t polyphony;
  int16_t model;
};

struct RingsV1State {
  int16_t structure;
  int16_t damping;
  int16_t brightness;
  int16_t note;
  int16_t position;
  int16_t slide;
  int16_t polyphony;
  int16_t model;
};

static uint8_t polyphonyValue(int16_t setting) {
  static const uint8_t values[] = {1, 2, 4};
  return values[constrain(setting, 0, 2)];
}

static void sanitizeRingsState() {
  rings_structure = constrain(rings_structure, 0, 1000);
  rings_damping = constrain(rings_damping, 0, 1000);
  rings_brightness = constrain(rings_brightness, 0, 1000);
  rings_freq = constrain(rings_freq, 20, 2000);
  rings_trigger = constrain(rings_trigger, 0, 2);
  rings_position = constrain(rings_position, 0, 1000);
  rings_slide = constrain(rings_slide, 0, 1000);
  rings_polyphony = constrain(rings_polyphony, 0, 2);
  rings_model = constrain(rings_model, 0, (int16_t)pico_rings::kNumModels - 1);
}

static bool loadRingsState() {
  RingsState state;
  if (PicoAppStateLoad(RINGS_STATE_TAG, &state, sizeof(state))) {
    rings_structure = state.structure;
    rings_damping = state.damping;
    rings_brightness = state.brightness;
    rings_freq = state.freq;
    rings_trigger = state.trigger;
    rings_position = state.position;
    rings_slide = state.slide;
    rings_polyphony = state.polyphony;
    rings_model = state.model;
    PicoCVImportState(&state.cv);
    sanitizeRingsState();
    return true;
  } else {
    RingsV2State v2;
    if (PicoAppStateLoad(RINGS_V2_STATE_TAG, &v2, sizeof(v2))) {
      rings_structure = v2.structure;
      rings_damping = v2.damping;
      rings_brightness = v2.brightness;
      rings_freq = (int16_t)(440.0f * powf(2.0f, (v2.note * 0.1f - 69.0f) / 12.0f) + 0.5f);
      rings_trigger = v2.trigger;
      rings_position = v2.position;
      rings_slide = v2.slide;
      rings_polyphony = v2.polyphony;
      rings_model = v2.model;
    } else {
      RingsV1State v1;
      if (!PicoAppStateLoad(RINGS_V1_STATE_TAG, &v1, sizeof(v1))) return false;
      rings_structure = v1.structure;
      rings_damping = v1.damping;
      rings_brightness = v1.brightness;
      rings_freq = (int16_t)(440.0f * powf(2.0f, (v1.note * 0.1f - 69.0f) / 12.0f) + 0.5f);
      rings_position = v1.position;
      rings_slide = v1.slide;
      rings_polyphony = v1.polyphony;
      rings_model = v1.model;
    }
  }
  sanitizeRingsState();
  return false;
}

static void saveRingsState() {
  sanitizeRingsState();
  RingsState state = {rings_structure, rings_damping, rings_brightness,
                      rings_freq, rings_trigger, rings_position, rings_slide,
                      rings_polyphony, rings_model};
  PicoCVExportState(&state.cv);
  PicoAppStateSave(RINGS_STATE_TAG, &state, sizeof(state));
}

void updateRingsPatch(int16_t, int16_t) { sanitizeRingsState(); }
#include "Rings_Menu.h"
#include "MenuSystem.h"

static void alarm_in_us_arm(uint32_t delay_us);
static void alarm_irq() {
  menuenc.service();
  hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
  alarm_in_us_arm(TIMER_MICROS);
}
static void alarm_in_us_arm(uint32_t delay_us) {
  timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + delay_us;
}
static void alarm_in_us(uint32_t delay_us) {
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
  irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
  irq_set_enabled(ALARM_IRQ, true);
  alarm_in_us_arm(delay_us);
}

static float ringsPitchNote(int16_t slide_value) {
  const float frequency = PicoCVModulatedFrequencyHz(
      3, (float)rings_freq, (float)menus[3].min, (float)menus[3].max);
  const float target = 69.0f + 12.0f * log2f(frequency / 440.0f);
  const uint32_t now = micros();
  if (!pitch_initialized) {
    smoothed_cv_note = target;
    pitch_update_us = now;
    pitch_initialized = true;
  } else {
    const float dt = (now - pitch_update_us) * 1.0e-6f;
    pitch_update_us = now;
    const float slide = slide_value * 0.001f;
    const float slew = 0.001f + 0.299f * slide * slide;
    const float delta = target - smoothed_cv_note;
    if (fabsf(delta) > 0.05f) smoothed_cv_note += delta * constrain(dt / (slew + dt), 0.0f, 1.0f);
  }
  return constrain(smoothed_cv_note, pico_rings::kMinNote, pico_rings::kMaxNote);
}

static void serviceRingsControls() {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if ((now - last_ms) < 5) return;
  last_ms = now;

  bool trigger = false;
  if (rings_trigger != 0) {
    const uint16_t raw = rings_trigger == 1 ? sampleCV1() : sampleCV2();
    const uint16_t value = (AD_RANGE - 1u) - raw;
    if (trigger_high) {
      trigger = value >= (AD_RANGE / 3u);
    } else {
      trigger = value > ((AD_RANGE * 2u) / 3u);
    }
  }
  if (trigger && !trigger_high) pico_rings::QueueTrigger();
  trigger_high = trigger;

  const int16_t structure = PicoCVModulatedValue(
      0, rings_structure, menus[0].min, menus[0].max);
  const int16_t damping = PicoCVModulatedValue(
      1, rings_damping, menus[1].min, menus[1].max);
  const int16_t brightness = PicoCVModulatedValue(
      2, rings_brightness, menus[2].min, menus[2].max);
  const int16_t position = PicoCVModulatedValue(
      5, rings_position, menus[5].min, menus[5].max);
  const int16_t slide = PicoCVModulatedValue(
      6, rings_slide, menus[6].min, menus[6].max);
  const int16_t polyphony = PicoCVModulatedValue(
      7, rings_polyphony, menus[7].min, menus[7].max);
  const int16_t model = PicoCVModulatedValue(
      8, rings_model, menus[8].min, menus[8].max);

  pico_rings::SetUiState(structure * 0.001f,
                         damping * 0.001f,
                         brightness * 0.001f,
                         ringsPitchNote(slide),
                         position * 0.001f,
                         polyphonyValue(polyphony), model);
}

static void prepareRingsExit() {
  pico_rings::Mute(true);
  delay(32);
}

void setup() {
  pinMode(ENCA_IN, INPUT_PULLUP);
  pinMode(ENCB_IN, INPUT_PULLUP);
  pinMode(ENCSW_IN, INPUT_PULLUP);
  analogReadResolution(AD_BITS);
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();
  alarm_in_us(TIMER_MICROS);
  PicoCVBindMenus(menus, NUM_MENUS);
  const bool cv_state_loaded = loadRingsState();
  if (!cv_state_loaded) {
    PicoCVCommitAssignment(3, 1, PICOPRO_CV_VOCT_AMOUNT);
  }
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) while (true) {}
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  displaytimer = millis();
  drawmenu(0);
  menuenc.getValue();

  pico_rings::Init();
  rings_dac.setDOUT(I2S_DATA);
  rings_dac.setBCLK(BCLK);
  rings_dac.setMCLK(MCLK);
  rings_dac.setMCLKmult(256);
  rings_dac.setBitsPerSample(32);
  rings_dac.setFrequency(SAMPLERATE);
  rings_dac.begin();
  rings_audio_ready = true;
}

void loop() {
  PicoProServiceSelectorExit(ENCSW_IN, menuenc, saveRingsState, prepareRingsExit);
  serviceRingsControls();
  switch (UI_state) {
    case RUN: domenus(); break;
    case DORMANT:
      if (menuenc.getValue() || !digitalRead(ENCSW_IN)) {
        UI_state = WAIT_BUTTON_RELEASE;
        display.ssd1306_command(SSD1306_DISPLAYON);
        drawmenu(menuindex);
      }
      break;
    case WAIT_BUTTON_RELEASE:
      if (digitalRead(ENCSW_IN)) UI_state = RUN;
      break;
  }
  if ((millis() - displaytimer) > DISPLAY_BLANK_MS && UI_state == RUN) {
    UI_state = DORMANT;
    blankdisplay();
  }
}

void setup1() {
  while (!rings_audio_ready) tight_loop_contents();
}

void loop1() {
  const int32_t sample = static_cast<int32_t>(pico_rings::NextSample()) << 16;
  rings_dac.write(sample);
  rings_dac.write(sample);
}
