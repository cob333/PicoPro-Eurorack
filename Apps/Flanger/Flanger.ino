// Stereo Flanger for PicoPro, adapted from the PicoFX Flanger.

#include "PicoPro.h"
#include <I2S.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ClickEncoder.h"
#include "PicoAppState.h"
#include "Flanger.h"

#define SAMPLERATE 44100
#define DEBOUNCE_CYCLES 100

enum UISTATES { RUN, DORMANT, WAIT_BUTTON_RELEASE };
int16_t UI_state = RUN;

I2S i2s(INPUT_PULLUP);
ClickEncoder menuenc(ENCA_IN, ENCB_IN, ENCSW_IN, ENCDIVIDE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
int32_t displaytimer;

#include "ui/CVModulation.h"

int16_t flanger_delay = 750;
int16_t flanger_feedback = 200;
int16_t flanger_rate = 300;
int16_t flanger_depth = 900;
int16_t flanger_mix = 500;
int16_t flanger_level = 800;
int16_t flanger_width = 1000;
int16_t flanger_wave = 0;

static volatile int16_t active_delay = 750;
static volatile int16_t active_feedback = 200;
static volatile int16_t active_rate = 300;
static volatile int16_t active_depth = 900;
static volatile int16_t active_mix = 500;
static volatile int16_t active_level = 800;
static volatile int16_t active_width = 1000;
static volatile int16_t active_wave = 0;
static volatile int16_t flanger_exit_gain = 1000;

class PicoFlanger {
 public:
  enum LfoWaveform : uint8_t {
    WAVE_TRIANGLE = 0, WAVE_SINE, WAVE_SQUARE, WAVE_SAW,
    WAVE_RANDOM_SMOOTH, WAVE_RANDOM_STEPPED, WAVE_LAST
  };

  void Init(float sample_rate, float phase, uint32_t seed) {
    sample_rate_ = sample_rate;
    delay_line_.Init();
    lfo_phase_ = phase;
    waveform_ = WAVE_TRIANGLE;
    random_state_ = seed ? seed : 1u;
    random_from_ = RandomBipolar();
    random_to_ = RandomBipolar();
    random_hold_ = RandomBipolar();
    SetFeedback(0.2f);
    SetDelay(0.75f);
    SetLfoFreq(0.3f);
    SetLfoDepth(0.9f);
  }

  float Process(float input) {
    delay_line_.SetDelay(1.0f + delay_samples_ + ProcessLfo());
    const float delayed = delay_line_.Read();
    delay_line_.Write(input + delayed * feedback_);
    return (input + delayed) * 0.5f;
  }

  void SetFeedback(float value) {
    feedback_ = daisysp::fclamp(value, 0.0f, 1.0f) * 0.97f;
  }
  void SetDelay(float value) {
    delay_samples_ = (0.1f + daisysp::fclamp(value, 0.0f, 1.0f) * 6.9f) *
                     0.001f * sample_rate_;
    lfo_amplitude_ = fminf(lfo_amplitude_, delay_samples_);
  }
  void SetLfoFreq(float value) {
    lfo_increment_ = daisysp::fclamp(value / sample_rate_, 0.0f, 0.25f);
  }
  void SetLfoDepth(float value) {
    lfo_amplitude_ = daisysp::fclamp(value, 0.0f, 0.93f) * delay_samples_;
  }
  void SetWaveform(uint8_t value) {
    waveform_ = value < WAVE_LAST ? value : WAVE_TRIANGLE;
  }

 private:
  static constexpr size_t kDelayLength = 960;
  static constexpr float kTwoPi = 6.28318530717958647692f;

  float ProcessLfo() {
    lfo_phase_ += lfo_increment_;
    while (lfo_phase_ >= 1.0f) {
      lfo_phase_ -= 1.0f;
      random_from_ = random_to_;
      random_to_ = RandomBipolar();
      random_hold_ = RandomBipolar();
    }
    float value;
    switch (waveform_) {
      case WAVE_SINE: value = sinf(lfo_phase_ * kTwoPi); break;
      case WAVE_SQUARE: value = lfo_phase_ < 0.5f ? 1.0f : -1.0f; break;
      case WAVE_SAW: value = lfo_phase_ * 2.0f - 1.0f; break;
      case WAVE_RANDOM_SMOOTH:
        value = random_from_ + (random_to_ - random_from_) * lfo_phase_;
        break;
      case WAVE_RANDOM_STEPPED: value = random_hold_; break;
      default: value = 1.0f - 4.0f * fabsf(lfo_phase_ - 0.5f); break;
    }
    return value * lfo_amplitude_;
  }

  float RandomBipolar() {
    random_state_ = random_state_ * 1664525u + 1013904223u;
    return ((random_state_ >> 8) * (1.0f / 8388607.5f)) - 1.0f;
  }

  float sample_rate_ = SAMPLERATE;
  float feedback_ = 0.2f;
  float lfo_phase_ = 0.0f;
  float lfo_increment_ = 0.0f;
  float lfo_amplitude_ = 0.0f;
  float delay_samples_ = 1.0f;
  uint8_t waveform_ = WAVE_TRIANGLE;
  uint32_t random_state_ = 1;
  float random_from_ = 0.0f;
  float random_to_ = 0.0f;
  float random_hold_ = 0.0f;
  daisysp::DelayLine<float, kDelayLength> delay_line_;
};

PicoFlanger flanger_left;
PicoFlanger flanger_right;

#define FLANGER_STATE_TAG PICOPRO_APP_STATE_TAG('F', 'L', 'G', '1')
struct FlangerState {
  int16_t delay;
  int16_t feedback;
  int16_t rate;
  int16_t depth;
  int16_t mix;
  int16_t level;
  int16_t width;
  int16_t wave;
  PicoCVPersistentState cv;
};

static void sanitizeFlangerState() {
  flanger_delay = constrain(flanger_delay, 0, 1000);
  flanger_feedback = constrain(flanger_feedback, 0, 1000);
  flanger_rate = constrain(flanger_rate, 0, 1000);
  flanger_depth = constrain(flanger_depth, 0, 930);
  flanger_mix = constrain(flanger_mix, 0, 1000);
  flanger_level = constrain(flanger_level, 0, 1000);
  flanger_width = constrain(flanger_width, 0, 1000);
  flanger_wave = constrain(flanger_wave, 0, 5);
}

static void loadFlangerState() {
  FlangerState state;
  if (!PicoAppStateLoad(FLANGER_STATE_TAG, &state, sizeof(state))) return;
  flanger_delay = state.delay;
  flanger_feedback = state.feedback;
  flanger_rate = state.rate;
  flanger_depth = state.depth;
  flanger_mix = state.mix;
  flanger_level = state.level;
  flanger_width = state.width;
  flanger_wave = state.wave;
  PicoCVImportState(&state.cv);
  sanitizeFlangerState();
}

static void saveFlangerState() {
  sanitizeFlangerState();
  FlangerState state = {flanger_delay, flanger_feedback, flanger_rate,
                        flanger_depth, flanger_mix, flanger_level,
                        flanger_width, flanger_wave};
  PicoCVExportState(&state.cv);
  PicoAppStateSave(FLANGER_STATE_TAG, &state, sizeof(state));
}

void updateFlangerMenu(int16_t, int16_t) { sanitizeFlangerState(); }
#include "Flanger_Menu.h"
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

static void serviceFlangerCV() {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if ((now - last_ms) < 10) return;
  last_ms = now;
  active_delay = PicoCVModulatedValue(0, flanger_delay, menus[0].min, menus[0].max);
  active_feedback = PicoCVModulatedValue(1, flanger_feedback, menus[1].min, menus[1].max);
  active_rate = PicoCVModulatedValue(2, flanger_rate, menus[2].min, menus[2].max);
  active_depth = PicoCVModulatedValue(3, flanger_depth, menus[3].min, menus[3].max);
  active_mix = PicoCVModulatedValue(4, flanger_mix, menus[4].min, menus[4].max);
  active_level = PicoCVModulatedValue(5, flanger_level, menus[5].min, menus[5].max);
  active_width = PicoCVModulatedValue(6, flanger_width, menus[6].min, menus[6].max);
  active_wave = PicoCVModulatedValue(7, flanger_wave, menus[7].min, menus[7].max);
}

static void prepareFlangerExit() {
  flanger_exit_gain = 0;
  delay(32);
}

static int32_t FloatToI2S(float value) {
  value = daisysp::fclamp(value, -1.0f, 1.0f);
  return static_cast<int32_t>(value * 2147483647.0f);
}

void setup() {
  pinMode(ENCA_IN, INPUT_PULLUP);
  pinMode(ENCB_IN, INPUT_PULLUP);
  pinMode(ENCSW_IN, INPUT_PULLUP);
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();
  alarm_in_us(TIMER_MICROS);
  analogReadResolution(AD_BITS);

  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK);
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(SAMPLERATE);
  i2s.begin();

  PicoCVBindMenus(menus, NUM_MENUS);
  loadFlangerState();
  serviceFlangerCV();
  flanger_left.Init(SAMPLERATE, 0.0f, 0x12345678u);
  flanger_right.Init(SAMPLERATE, 0.5f, 0x87654321u);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) while (true) {}
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  displaytimer = millis();
  drawmenu(0);
  menuenc.getValue();
}

void loop() {
  static int16_t debounce_counter = 0;
  PicoProServiceSelectorExit(ENCSW_IN, menuenc, saveFlangerState, prepareFlangerExit);
  serviceFlangerCV();
  switch (UI_state) {
    case RUN: domenus(); break;
    case DORMANT:
      if (menuenc.getValue() || !digitalRead(ENCSW_IN)) {
        UI_state = WAIT_BUTTON_RELEASE;
        debounce_counter = DEBOUNCE_CYCLES;
        drawmenu(menuindex);
      }
      break;
    case WAIT_BUTTON_RELEASE:
      if (digitalRead(ENCSW_IN) && --debounce_counter <= 0) UI_state = RUN;
      break;
  }
  if ((millis() - displaytimer) > DISPLAY_BLANK_MS && UI_state == RUN) {
    UI_state = DORMANT;
    blankdisplay();
  }
}

void setup1() { delay(1000); }

void loop1() {
  static int16_t last_delay = -1, last_feedback = -1, last_rate = -1;
  static int16_t last_depth = -1, last_wave = -1;
  static int16_t exit_gain = 1000;
  const int16_t delay_value = active_delay;
  const int16_t feedback = active_feedback;
  const int16_t rate = active_rate;
  const int16_t depth = active_depth;
  const int16_t wave = active_wave;
  if (delay_value != last_delay) {
    flanger_left.SetDelay(delay_value * 0.001f);
    flanger_right.SetDelay(delay_value * 0.001f);
    last_delay = delay_value;
    last_depth = -1;
  }
  if (feedback != last_feedback) {
    flanger_left.SetFeedback(feedback * 0.001f);
    flanger_right.SetFeedback(feedback * 0.001f);
    last_feedback = feedback;
  }
  if (rate != last_rate) {
    flanger_left.SetLfoFreq(rate * 0.001f);
    flanger_right.SetLfoFreq(rate * 0.001f);
    last_rate = rate;
  }
  if (depth != last_depth) {
    flanger_left.SetLfoDepth(depth * 0.001f);
    flanger_right.SetLfoDepth(depth * 0.001f);
    last_depth = depth;
  }
  if (wave != last_wave) {
    flanger_left.SetWaveform(wave);
    flanger_right.SetWaveform(wave);
    last_wave = wave;
  }

  const float input_left = i2s.read() * DIV_16;
  const float input_right = i2s.read() * DIV_16;
  const float wet_left = flanger_left.Process(input_left);
  const float wet_right = flanger_right.Process(input_right);
  const float wet_mid = (wet_left + wet_right) * 0.5f;
  const float width = active_width * 0.001f;
  const float stereo_left = wet_mid + (wet_left - wet_mid) * width;
  const float stereo_right = wet_mid + (wet_right - wet_mid) * width;
  const float mix = active_mix * 0.001f;
  const float level = active_level * 0.001f;
  if (exit_gain > flanger_exit_gain) --exit_gain;
  const float output_gain = level * exit_gain * 0.001f;
  i2s.write(FloatToI2S((input_left * (1.0f - mix) + stereo_left * mix) * output_gain));
  i2s.write(FloatToI2S((input_right * (1.0f - mix) + stereo_right * mix) * output_gain));
}
