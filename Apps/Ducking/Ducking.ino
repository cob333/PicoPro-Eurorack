// Stereo trigger ducking for PicoPro, adapted from PicoFX Sidechain.

#include "PicoPro.h"
#include <I2S.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ClickEncoder.h"
#include "PicoAppState.h"

#define SAMPLERATE 44100
#define DEBOUNCE_CYCLES 100

constexpr float DUCKING_MIN_GAIN = 0.10f;
constexpr uint8_t DUCKING_TRIGGER_POLL_DIVIDER = 16;

enum UISTATES { RUN, DORMANT, WAIT_BUTTON_RELEASE };
int16_t UI_state = RUN;
int32_t displaytimer;

I2S i2s(INPUT_PULLUP);
ClickEncoder menuenc(ENCA_IN, ENCB_IN, ENCSW_IN, ENCDIVIDE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#include "ui/CVModulation.h"

int16_t ducking_attack = 20;
int16_t ducking_decay = 250;
int16_t ducking_knee = 500;
int16_t ducking_level = 850;
int16_t ducking_trigger = 2;

struct DuckingSettings {
  volatile uint32_t revision;
  volatile float attack_ms;
  volatile float decay_ms;
  volatile float knee;
  volatile float level;
  volatile int16_t trigger;
};

static DuckingSettings ducking_settings = {0, 20.0f, 250.0f, 0.5f, 0.85f, 2};
static volatile int16_t ducking_exit_gain = 1000;
static volatile bool ducking_audio_ready = false;
static volatile uint16_t ducking_meter_current_q15 = 0;
static volatile uint16_t ducking_meter_peak_q15 = 0;
static uint16_t ducking_meter_draw_q15 = 0;

class StereoTriggerDucker {
 public:
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    stage_ = IDLE;
    progress_ = 1.0f;
    current_duck_ = 0.0f;
    segment_start_ = 0.0f;
    segment_end_ = 0.0f;
    increment_ = 1.0f;
    attack_inc_ = 1.0f;
    decay_inc_ = 1.0f;
    curve_ = 0.5f;
    depth_ = 1.0f - DUCKING_MIN_GAIN;
    SetAttackMs(20.0f);
    SetDecayMs(250.0f);
  }

  void SetAttackMs(float ms) { attack_inc_ = 1.0f / TimeToSamples(ms); }
  void SetDecayMs(float ms) { decay_inc_ = 1.0f / TimeToSamples(ms); }
  void SetCurve(float curve) { curve_ = constrain(curve, 0.0f, 1.0f); }

  void Trigger() { StartSegment(current_duck_, 1.0f, attack_inc_, ATTACK); }

  float NextGain() {
    AdvanceEnvelope();
    return 1.0f - current_duck_ * depth_;
  }

  float GetDuckAmount() const { return current_duck_; }

 private:
  enum EnvelopeStage : uint8_t { IDLE = 0, ATTACK, DECAY };

  float TimeToSamples(float ms) const {
    const float samples = max(0.1f, ms) * 0.001f * sample_rate_;
    return max(1.0f, samples);
  }

  float Shape(float x) const {
    const float log_shape = x * (2.0f - x);
    const float exp_shape = x * x;
    if (curve_ < 0.5f) {
      return log_shape + (x - log_shape) * (curve_ * 2.0f);
    }
    return x + (exp_shape - x) * ((curve_ - 0.5f) * 2.0f);
  }

  void StartSegment(float start, float end, float increment,
                    EnvelopeStage stage) {
    segment_start_ = start;
    segment_end_ = end;
    progress_ = 0.0f;
    increment_ = increment;
    stage_ = stage;
    current_duck_ = start;
  }

  void AdvanceEnvelope() {
    if (stage_ == IDLE) return;
    progress_ += increment_;
    if (progress_ >= 1.0f) {
      current_duck_ = segment_end_;
      if (stage_ == ATTACK) {
        StartSegment(current_duck_, 0.0f, decay_inc_, DECAY);
      } else {
        stage_ = IDLE;
        current_duck_ = 0.0f;
      }
      return;
    }
    const float shaped = Shape(progress_);
    current_duck_ = segment_start_ + (segment_end_ - segment_start_) * shaped;
  }

  float sample_rate_ = SAMPLERATE;
  float attack_inc_ = 1.0f;
  float decay_inc_ = 1.0f;
  float increment_ = 1.0f;
  float progress_ = 1.0f;
  float segment_start_ = 0.0f;
  float segment_end_ = 0.0f;
  float current_duck_ = 0.0f;
  float curve_ = 0.5f;
  float depth_ = 1.0f - DUCKING_MIN_GAIN;
  EnvelopeStage stage_ = IDLE;
};

static StereoTriggerDucker ducker;

#define DUCKING_STATE_TAG PICOPRO_APP_STATE_TAG('D', 'U', 'C', '1')
struct DuckingState {
  int16_t attack;
  int16_t decay;
  int16_t knee;
  int16_t level;
  int16_t trigger;
  PicoCVPersistentState cv;
};

static void sanitizeDuckingState() {
  ducking_attack = constrain(ducking_attack, 1, 250);
  ducking_decay = constrain(ducking_decay, 10, 2000);
  ducking_knee = constrain(ducking_knee, 0, 1000);
  ducking_level = constrain(ducking_level, 0, 1000);
  ducking_trigger = constrain(ducking_trigger, 0, 2);
}

static void loadDuckingState() {
  DuckingState state;
  if (!PicoAppStateLoad(DUCKING_STATE_TAG, &state, sizeof(state))) return;
  ducking_attack = state.attack;
  ducking_decay = state.decay;
  ducking_knee = state.knee;
  ducking_level = state.level;
  ducking_trigger = state.trigger;
  PicoCVImportState(&state.cv);
  sanitizeDuckingState();
}

static void saveDuckingState() {
  sanitizeDuckingState();
  DuckingState state = {ducking_attack, ducking_decay, ducking_knee,
                        ducking_level, ducking_trigger};
  PicoCVExportState(&state.cv);
  PicoAppStateSave(DUCKING_STATE_TAG, &state, sizeof(state));
}

void updateDuckingMenu(int16_t, int16_t) { sanitizeDuckingState(); }
#include "Ducking_Menu.h"
static void drawDuckingMenuItem(const menu *item, bool editing,
                                int8_t nav_dir, uint8_t index, uint8_t total);
#define PICOPRO_MENU_DRAW_ITEM(item, editing, nav_dir, index, total) \
  drawDuckingMenuItem((item), (editing), (nav_dir), (index), (total))
#include "MenuSystem.h"
#undef PICOPRO_MENU_DRAW_ITEM

struct DuckingMeterPoint {
  int8_t x;
  int8_t y;
};

static const DuckingMeterPoint DUCKING_METER_NEEDLE_POINTS[21] = {
  {9, -4}, {9, -5}, {8, -6}, {7, -7}, {6, -8},
  {5, -9}, {4, -9}, {3, -10}, {2, -10}, {1, -10},
  {0, -10}, {-1, -10}, {-2, -10}, {-3, -10}, {-4, -9},
  {-5, -9}, {-6, -8}, {-7, -7}, {-8, -6}, {-9, -5}, {-9, -4},
};

static void drawDuckingMeterFace(uint16_t duck_q15) {
  constexpr int16_t cx = 32;
  constexpr int16_t cy = 19;
  // Erase the complete generic knob footprint before drawing the shorter VU.
  display.fillRect(20, 1, 25, 20, BLACK);

  // Radius-10 upper semicircle; the baseline remains fixed at y=19.
  display.drawLine(22, 19, 23, 15, WHITE);
  display.drawLine(23, 15, 25, 12, WHITE);
  display.drawLine(25, 12, 28, 10, WHITE);
  display.drawLine(28, 10, 32, 9, WHITE);
  display.drawLine(32, 9, 36, 10, WHITE);
  display.drawLine(36, 10, 39, 12, WHITE);
  display.drawLine(39, 12, 41, 15, WHITE);
  display.drawLine(41, 15, 42, 19, WHITE);
  display.drawLine(22, 19, 42, 19, WHITE);

  // Five sparse scale marks remain readable on the 64x32 OLED.
  display.drawPixel(24, 15, WHITE);
  display.drawPixel(27, 12, WHITE);
  display.drawPixel(32, 10, WHITE);
  display.drawPixel(37, 12, WHITE);
  display.drawPixel(40, 15, WHITE);

  if (duck_q15 > 32767u) duck_q15 = 32767u;
  uint8_t index = (uint32_t)duck_q15 * 20u / 32767u;
  const DuckingMeterPoint point = DUCKING_METER_NEEDLE_POINTS[index];
  display.drawPixel(cx - 1, cy - 1, WHITE);
  display.drawPixel(cx, cy - 1, WHITE);
  display.drawPixel(cx + 1, cy - 1, WHITE);
  display.drawPixel(cx, cy, WHITE);
  display.drawLine(cx, cy, cx + point.x, cy + point.y, WHITE);
}

static void drawDuckingMenuItem(const menu *item, bool editing,
                                int8_t nav_dir, uint8_t index, uint8_t total) {
  PicoKnobDrawMenuItem(item, editing, nav_dir, index, total);
  const uint16_t current = __atomic_load_n(&ducking_meter_current_q15,
                                            __ATOMIC_RELAXED);
  const uint16_t peak = __atomic_load_n(&ducking_meter_peak_q15,
                                         __ATOMIC_RELAXED);
  drawDuckingMeterFace(max(ducking_meter_draw_q15, max(current, peak)));
}

static void serviceDuckingMeterUI() {
  static uint32_t last_frame_ms = 0;
  static int8_t last_menu = -1;
  static int16_t last_value = INT16_MIN;
  static int16_t last_mode = -1;
  static uint8_t last_needle = 0xffu;
  static bool overlay_was_active = false;
  const uint32_t now = millis();
  if (UI_state != RUN) return;
  if (picoCVUiState != PICOPRO_CV_UI_OFF) {
    overlay_was_active = true;
    return;
  }
  if (overlay_was_active) {
    last_menu = -1;
    overlay_was_active = false;
  }
  if ((now - last_frame_ms) < 33u) return;
  last_frame_ms = now;

  const uint16_t current = __atomic_load_n(&ducking_meter_current_q15,
                                            __ATOMIC_RELAXED);
  const uint16_t peak = __atomic_exchange_n(&ducking_meter_peak_q15, 0,
                                             __ATOMIC_ACQ_REL);
  const uint16_t shown = max(current, peak);
  const uint8_t needle = (uint32_t)shown * 20u / 32767u;
  const int16_t value = PicoCVDisplayValue(menuindex,
                                            *menus[menuindex].parameter);
  if (last_menu == menuindex && last_value == value &&
      last_mode == menustate && last_needle == needle) return;
  last_menu = menuindex;
  last_value = value;
  last_mode = menustate;
  last_needle = needle;
  ducking_meter_draw_q15 = shown;
  drawDuckingMenuItem(&menus[menuindex], menustate == PARAM_INPUT,
                      0, menuindex, NUM_MENUS);
  display.display();
}

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

static void publishDuckingSettings(float attack_ms, float decay_ms,
                                   float knee, float level, int16_t trigger) {
  __atomic_add_fetch(&ducking_settings.revision, 1u, __ATOMIC_ACQ_REL);
  ducking_settings.attack_ms = attack_ms;
  ducking_settings.decay_ms = decay_ms;
  ducking_settings.knee = knee;
  ducking_settings.level = level;
  ducking_settings.trigger = trigger;
  __atomic_add_fetch(&ducking_settings.revision, 1u, __ATOMIC_RELEASE);
}

static void serviceDuckingControls() {
  static uint32_t last_ms = 0;
  static int8_t digital_trigger_input = 0;
  const uint32_t now = millis();
  PicoCVInputSelectDigitalRole((int8_t)ducking_trigger,
                               &digital_trigger_input);
  if ((now - last_ms) < 5) return;
  last_ms = now;

  const int16_t attack = PicoCVModulatedValue(
      0, ducking_attack, menus[0].min, menus[0].max);
  const int16_t decay = PicoCVModulatedValue(
      1, ducking_decay, menus[1].min, menus[1].max);
  const int16_t knee = PicoCVModulatedValue(
      2, ducking_knee, menus[2].min, menus[2].max);
  const int16_t level = PicoCVModulatedValue(
      3, ducking_level, menus[3].min, menus[3].max);
  publishDuckingSettings((float)attack, (float)decay,
                         knee * 0.001f, level * 0.001f, ducking_trigger);

}

static void prepareDuckingExit() {
  ducking_exit_gain = 0;
  delay(32);
}

static int32_t duckingFloatToI2S(float value) {
  value = constrain(value, -1.0f, 1.0f);
  return (int32_t)(value * 2147483647.0f);
}

void setup() {
  pinMode(ENCA_IN, INPUT_PULLUP);
  pinMode(ENCB_IN, INPUT_PULLUP);
  pinMode(ENCSW_IN, INPUT_PULLUP);
  PicoCVInputBegin();
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();
  alarm_in_us(TIMER_MICROS);

  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK);
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(SAMPLERATE);
  i2s.begin();

  PicoCVBindMenus(menus, NUM_MENUS);
  loadDuckingState();
  serviceDuckingControls();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) while (true) {}
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  displaytimer = millis();
  drawmenu(0);
  menuenc.getValue();
  ducking_audio_ready = true;
}

void loop() {
  static int16_t debounce_counter = 0;
  PicoProServiceSelectorExit(ENCSW_IN, menuenc, saveDuckingState,
                             prepareDuckingExit);
  serviceDuckingControls();
  switch (UI_state) {
    case RUN: domenus(); break;
    case DORMANT:
      if (menuenc.getValue() || !digitalRead(ENCSW_IN)) {
        UI_state = WAIT_BUTTON_RELEASE;
        debounce_counter = DEBOUNCE_CYCLES;
        display.ssd1306_command(SSD1306_DISPLAYON);
        drawmenu(menuindex);
      }
      break;
    case WAIT_BUTTON_RELEASE:
      if (digitalRead(ENCSW_IN) && --debounce_counter <= 0) UI_state = RUN;
      break;
  }
  serviceDuckingMeterUI();
  if ((millis() - displaytimer) > DISPLAY_BLANK_MS && UI_state == RUN) {
    UI_state = DORMANT;
    blankdisplay();
  }
}

void setup1() {
  while (!ducking_audio_ready) tight_loop_contents();
  ducker.Init(SAMPLERATE);
}

void loop1() {
  static uint8_t trigger_divider = 0;
  static bool trigger_state = false;
  static int16_t previous_trigger = -1;
  static uint32_t applied_revision = UINT32_MAX;
  static float output_level = 0.85f;
  static float control_attack_ms = 20.0f;
  static float control_decay_ms = 250.0f;
  static float control_knee = 0.5f;
  static float control_level = 0.85f;
  static int16_t control_trigger = 2;
  static int16_t exit_gain = 1000;

  const int32_t input_left_32 = i2s.read();
  const int32_t input_right_32 = i2s.read();

  if (++trigger_divider >= DUCKING_TRIGGER_POLL_DIVIDER) {
    trigger_divider = 0;
    uint32_t revision = applied_revision;
    for (uint8_t attempt = 0; attempt < 4u; ++attempt) {
      const uint32_t before = __atomic_load_n(&ducking_settings.revision,
                                               __ATOMIC_ACQUIRE);
      if (before & 1u) continue;
      const float attack_ms = ducking_settings.attack_ms;
      const float decay_ms = ducking_settings.decay_ms;
      const float knee = ducking_settings.knee;
      const float level = ducking_settings.level;
      const int16_t trigger = ducking_settings.trigger;
      const uint32_t after = __atomic_load_n(&ducking_settings.revision,
                                              __ATOMIC_ACQUIRE);
      if (before != after || (after & 1u)) continue;
      control_attack_ms = attack_ms;
      control_decay_ms = decay_ms;
      control_knee = knee;
      control_level = level;
      control_trigger = trigger;
      revision = after;
      break;
    }

    if (revision != applied_revision) {
      ducker.SetAttackMs(control_attack_ms);
      ducker.SetDecayMs(control_decay_ms);
      ducker.SetCurve(control_knee);
      applied_revision = revision;
    }
    output_level += (control_level - output_level) * 0.1f;

    if (control_trigger != previous_trigger) {
      trigger_state = false;
      previous_trigger = control_trigger;
    }
    if (control_trigger != 0) {
      const bool raw_trigger =
          PicoCVInputDigitalActiveLow((uint8_t)(control_trigger - 1));
      if (raw_trigger && !trigger_state) ducker.Trigger();
      trigger_state = raw_trigger;
    } else {
      trigger_state = false;
    }
  }

  const float gain = ducker.NextGain();
  if (trigger_divider == 0) {
    const uint16_t duck_q15 = (uint16_t)constrain(
        (int32_t)(ducker.GetDuckAmount() * 32767.0f + 0.5f), 0, 32767);
    __atomic_store_n(&ducking_meter_current_q15, duck_q15, __ATOMIC_RELAXED);
    uint16_t previous_peak = __atomic_load_n(&ducking_meter_peak_q15,
                                              __ATOMIC_RELAXED);
    for (uint8_t attempt = 0;
         attempt < 4u && duck_q15 > previous_peak;
         ++attempt) {
      if (__atomic_compare_exchange_n(&ducking_meter_peak_q15, &previous_peak,
                                      duck_q15, false, __ATOMIC_RELEASE,
                                      __ATOMIC_RELAXED)) {
        break;
      }
    }
  }
  if (exit_gain > ducking_exit_gain) --exit_gain;
  const float total_gain = gain * output_level * exit_gain * 0.001f;
  const float left = input_left_32 * DIV_16 * total_gain;
  const float right = input_right_32 * DIV_16 * total_gain;
  i2s.write(duckingFloatToI2S(left));
  i2s.write(duckingFloatToI2S(right));
}
