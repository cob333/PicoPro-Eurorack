// Stereo clock-sliced beat breaker for PicoPro.

#include "PicoPro.h"
#include <I2S.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ClickEncoder.h"
#include "PicoAppState.h"

#define SAMPLERATE 44100
#define DEBOUNCE_CYCLES 100

// Three stereo slots use the same 288 KB history budget as the reference
// sketch's six mono slots, while preserving its 24000-frame capture window.
constexpr uint8_t GLITCH_HISTORY_SLOTS = 3;
constexpr uint32_t GLITCH_MAX_BEAT_FRAMES = 24000;
constexpr uint32_t GLITCH_MIN_CLOCK_FRAMES = 96;
constexpr uint32_t GLITCH_CLOCK_TIMEOUT_FRAMES = SAMPLERATE * 2u;
constexpr uint32_t GLITCH_CLOCK_DEBOUNCE_FRAMES = 64;
constexpr uint32_t GLITCH_FADE_FRAMES = 64;
constexpr uint16_t GLITCH_Q16_MAX = 65535u;

enum UISTATES { RUN, DORMANT, WAIT_BUTTON_RELEASE };
int16_t UI_state = RUN;
int32_t displaytimer;

I2S i2s(INPUT_PULLUP);
ClickEncoder menuenc(ENCA_IN, ENCB_IN, ENCSW_IN, ENCDIVIDE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#include "ui/CVModulation.h"

int16_t glitch_break = 500;
int16_t glitch_reverse = 250;
int16_t glitch_repeat = 250;
int16_t glitch_level = 900;
int16_t glitch_clock = 0;
int16_t glitch_bpm = 120;
int16_t glitch_clock_ratio = 1;

static volatile int16_t active_break = 500;
static volatile int16_t active_reverse = 250;
static volatile int16_t active_repeat = 250;
static volatile int16_t active_level = 900;
static volatile int16_t active_clock = 0;
static volatile int16_t active_bpm = 120;
static volatile int16_t active_clock_ratio = 1;
static volatile int16_t glitch_exit_gain = 1000;
static volatile bool glitch_audio_ready = false;

enum GlitchVisualEvent : uint8_t {
  GLITCH_VISUAL_BREAK = 1u,
  GLITCH_VISUAL_REVERSE = 2u,
  GLITCH_VISUAL_REPEAT = 4u,
};
static volatile uint8_t glitch_visual_events = 0;

struct StereoFrame { int16_t left; int16_t right; };
static StereoFrame history[GLITCH_HISTORY_SLOTS][GLITCH_MAX_BEAT_FRAMES];
static uint16_t history_length[GLITCH_HISTORY_SLOTS];
static bool history_valid[GLITCH_HISTORY_SLOTS];

struct PlaybackState {
  uint8_t record_slot;
  uint32_t record_pos;
  uint32_t beat_pos;
  uint32_t samples_since_edge;
  uint32_t expected_beat_samples;
  uint32_t raw_clock_stable_samples;
  bool clock_seen;
  bool raw_clock_state;
  bool debounced_clock_state;
  bool playback_active;
  bool playback_reverse;
  uint8_t playback_repeat_count;
  uint8_t playback_slot;
  uint16_t playback_length;
  int16_t clock_mode;
  int16_t clock_ratio;
  uint32_t raw_samples_since_edge;
  uint32_t subdivision_samples;
  uint32_t subdivision_countdown;
  uint8_t subdivisions_remaining;
  uint8_t division_counter;
  bool raw_clock_seen;
};

static PlaybackState audio_state;

#define GLITCH_STATE_TAG PICOPRO_APP_STATE_TAG('G', 'L', 'T', '2')
#define GLITCH_V1_STATE_TAG PICOPRO_APP_STATE_TAG('G', 'L', 'T', '1')
struct GlitchState {
  int16_t break_probability;
  int16_t reverse_probability;
  int16_t repeat_macro;
  int16_t level;
  int16_t clock;
  int16_t bpm;
  int16_t clock_ratio;
  PicoCVPersistentState cv;
};

struct GlitchV1State {
  int16_t break_probability;
  int16_t reverse_probability;
  int16_t repeat_macro;
  int16_t level;
  int16_t clock;
  int16_t bpm;
  PicoCVPersistentState cv;
};

static void sanitizeGlitchState() {
  glitch_break = constrain(glitch_break, 0, 1000);
  glitch_reverse = constrain(glitch_reverse, 0, 1000);
  glitch_repeat = constrain(glitch_repeat, 0, 1000);
  glitch_level = constrain(glitch_level, 0, 1000);
  glitch_clock = constrain(glitch_clock, 0, 2);
  glitch_bpm = constrain(glitch_bpm, 20, 200);
  glitch_clock_ratio = constrain(glitch_clock_ratio, 0, 4);
}

static void loadGlitchState() {
  GlitchState state;
  if (PicoAppStateLoad(GLITCH_STATE_TAG, &state, sizeof(state))) {
    glitch_break = state.break_probability;
    glitch_reverse = state.reverse_probability;
    glitch_repeat = state.repeat_macro;
    glitch_level = state.level;
    glitch_clock = state.clock;
    glitch_bpm = state.bpm;
    glitch_clock_ratio = state.clock_ratio;
    PicoCVImportState(&state.cv);
  } else {
    GlitchV1State legacy;
    if (!PicoAppStateLoad(GLITCH_V1_STATE_TAG, &legacy, sizeof(legacy))) return;
    glitch_break = legacy.break_probability;
    glitch_reverse = legacy.reverse_probability;
    glitch_repeat = legacy.repeat_macro;
    glitch_level = legacy.level;
    glitch_clock = legacy.clock;
    glitch_bpm = legacy.bpm;
    PicoCVImportState(&legacy.cv);
  }
  sanitizeGlitchState();
}

static void saveGlitchState() {
  sanitizeGlitchState();
  GlitchState state = {glitch_break, glitch_reverse, glitch_repeat,
                       glitch_level, glitch_clock, glitch_bpm,
                       glitch_clock_ratio};
  PicoCVExportState(&state.cv);
  PicoAppStateSave(GLITCH_STATE_TAG, &state, sizeof(state));
}

static void syncGlitchTempoMenu();
void updateGlitchMenu(int16_t, int16_t) {
  sanitizeGlitchState();
  syncGlitchTempoMenu();
}
#include "Glitch_Menu.h"
#include "MenuSystem.h"

static void drawGlitchAnimatedKnob(uint8_t effects, uint8_t frame) {
  PicoKnobDrawMenuItem(&menus[menuindex], menustate == PARAM_INPUT,
                       0, menuindex, NUM_MENUS);

  // Replace only the central knob; index, CV indicators, arrows and value text
  // remain readable throughout the effect.
  display.fillRect(21, 1, 23, 19, BLACK);
  int16_t cx = PICOPRO_KNOB_CENTER_X;
  int16_t cy = PICOPRO_KNOB_CENTER_Y;

  const bool repeat_effect = effects & GLITCH_VISUAL_REPEAT;
  const bool reverse_effect = !repeat_effect && (effects & GLITCH_VISUAL_REVERSE);
  const bool break_effect = !repeat_effect && !reverse_effect &&
                            (effects & GLITCH_VISUAL_BREAK);

  if (break_effect) {
    static const int8_t jitter_x[] = {-3, 2, -1, 3, 0, -2, 1, 0};
    static const int8_t jitter_y[] = {1, -2, 2, 0, -1, 1, -1, 0};
    cx += jitter_x[frame & 7u];
    cy += jitter_y[frame & 7u];
  }

  const int16_t display_value = PicoCVDisplayValue(
      menuindex, *menus[menuindex].parameter);
  int16_t needle_index = PicoKnobNeedleIndex(
      display_value, menus[menuindex].min, menus[menuindex].max);
  if (break_effect) {
    needle_index = constrain(needle_index + ((frame & 1u) ? 5 : -4), 0, 30);
  }
  const PicoKnobPoint point = PICOPRO_KNOB_NEEDLE_POINTS[needle_index];

  if (repeat_effect) {
    // Three complete mini knobs separate and return like repeated echoes.
    const int8_t spread = 6 + (frame % 3u);
    const int8_t centers[3] = {(int8_t)(cx - spread), (int8_t)cx,
                               (int8_t)(cx + spread)};
    for (uint8_t i = 0; i < 3; ++i) {
      const int8_t mini_cx = centers[i];
      const uint8_t mini_index = (needle_index + i * 3u + frame) % 31u;
      const PicoKnobPoint mini_point = PICOPRO_KNOB_NEEDLE_POINTS[mini_index];
      display.drawCircle(mini_cx, cy, 4, WHITE);
      display.drawPixel(mini_cx, cy, WHITE);
      display.drawLine(mini_cx, cy,
                       mini_cx + mini_point.x / 2,
                       cy + mini_point.y / 2, WHITE);
    }
    // Short echo trails make the direction of duplication readable.
    display.drawPixel(cx - spread - 3, cy + ((frame & 1u) ? 2 : -2), WHITE);
    display.drawPixel(cx + spread + 3, cy + ((frame & 1u) ? -2 : 2), WHITE);
  } else if (reverse_effect) {
    // An asymmetric outline flows around the knob and changes volume each frame.
    const int8_t sway = (int8_t)(frame % 5u) - 2;
    const int8_t pulse = (frame & 1u) ? 1 : 0;
    display.drawLine(cx - 7 - pulse, cy - 2, cx - 4, cy - 7 + sway, WHITE);
    display.drawLine(cx - 4, cy - 7 + sway, cx + 3 + pulse, cy - 6 - sway, WHITE);
    display.drawLine(cx + 3 + pulse, cy - 6 - sway, cx + 8, cy - 1, WHITE);
    display.drawLine(cx + 8, cy - 1, cx + 5 - sway, cy + 6 + pulse, WHITE);
    display.drawLine(cx + 5 - sway, cy + 6 + pulse, cx - 3, cy + 8 - pulse, WHITE);
    display.drawLine(cx - 3, cy + 8 - pulse, cx - 7 - pulse, cy - 2, WHITE);
    const int8_t flow_x = cx - 5 + (frame * 2u) % 11u;
    display.drawPixel(flow_x, cy - 5 + ((frame & 1u) ? 1 : 0), WHITE);
    display.drawPixel(flow_x + 1, cy - 4 + ((frame & 1u) ? 1 : 0), WHITE);
    display.drawPixel(cx + 6 - (frame % 3u), cy + 7, WHITE);
    display.drawLine(cx, cy, cx + point.x, cy + point.y, WHITE);
  } else if (break_effect) {
    // Four displaced shards replace the original circle completely.
    const int8_t kick = frame & 1u;
    display.drawLine(cx - 8 - kick, cy - 2, cx - 5 - kick, cy - 7, WHITE);
    display.drawLine(cx - 5 - kick, cy - 7, cx - 1, cy - 6, WHITE);
    display.drawLine(cx + 2 + kick, cy - 7, cx + 7 + kick, cy - 3, WHITE);
    display.drawLine(cx + 7 + kick, cy - 3, cx + 6 + kick, cy + 1, WHITE);
    display.drawLine(cx + 7, cy + 3 + kick, cx + 2, cy + 8 + kick, WHITE);
    display.drawLine(cx + 2, cy + 8 + kick, cx - 1, cy + 6 + kick, WHITE);
    display.drawLine(cx - 3 - kick, cy + 7, cx - 8 - kick, cy + 3, WHITE);
    display.drawLine(cx - 8 - kick, cy + 3, cx - 6 - kick, cy, WHITE);
    display.drawLine(cx - 1, cy - 4, cx + point.x, cy + point.y, WHITE);
    display.drawPixel(cx + ((frame & 2u) ? 2 : -2), cy, WHITE);
  }

  if (break_effect) {
    const int8_t slice_y = 3 + ((frame * 3u) % 13u);
    display.drawFastHLine(22 + (frame & 3u), slice_y, 8, WHITE);
    display.drawFastHLine(35 - (frame & 3u), slice_y + 2, 8, WHITE);
  }
  updatedisplay();
}

static void serviceGlitchAnimation() {
  static uint8_t effects = 0;
  static uint8_t frame = 0;
  static uint32_t started_ms = 0;
  static uint32_t last_frame_ms = 0;
  const uint8_t events = __atomic_exchange_n(&glitch_visual_events, 0,
                                              __ATOMIC_RELAXED);
  const uint32_t now = millis();

  if (events) {
    effects = events;
    frame = 0;
    started_ms = now;
    last_frame_ms = now - 40u;
  }
  if (!effects || UI_state != RUN || picoCVUiState != PICOPRO_CV_UI_OFF) return;
  if ((now - started_ms) >= 320u) {
    effects = 0;
    drawmenu(menuindex, menustate == PARAM_INPUT);
    return;
  }
  if ((now - last_frame_ms) < 40u) return;
  last_frame_ms = now;
  drawGlitchAnimatedKnob(effects, frame++);
}

static void syncGlitchTempoMenu() {
  if (glitch_clock == 0) {
    menus[5].min = 20;
    menus[5].max = 200;
    menus[5].step = 1;
    menus[5].ptype = TYPE_INTEGER;
    menus[5].ptext = nullptr;
    menus[5].parameter = &glitch_bpm;
  } else {
    menus[5].min = 0;
    menus[5].max = 4;
    menus[5].step = 1;
    menus[5].ptype = TYPE_TEXT;
    menus[5].ptext = glitch_clock_ratio_names;
    menus[5].parameter = &glitch_clock_ratio;
  }
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

static void serviceGlitchControls() {
  static uint32_t last_ms = 0;
  static int8_t digital_clock_input = 0;
  const uint32_t now = millis();
  PicoCVInputSelectDigitalRole((int8_t)glitch_clock,
                               &digital_clock_input);
  if ((now - last_ms) < 5) return;
  last_ms = now;
  active_break = PicoCVModulatedValue(0, glitch_break, 0, 1000);
  active_reverse = PicoCVModulatedValue(1, glitch_reverse, 0, 1000);
  active_repeat = PicoCVModulatedValue(2, glitch_repeat, 0, 1000);
  active_level = PicoCVModulatedValue(3, glitch_level, 0, 1000);
  active_clock = glitch_clock;
  if (glitch_clock == 0) {
    active_bpm = PicoCVModulatedValue(5, glitch_bpm, 20, 200);
  } else {
    active_clock_ratio = glitch_clock_ratio;
  }
}

static inline uint16_t toQ16(int16_t value) {
  return (uint32_t)constrain(value, 0, 1000) * GLITCH_Q16_MAX / 1000u;
}
static inline bool chance(uint16_t probability) {
  if (!probability) return false;
  if (probability == GLITCH_Q16_MAX) return true;
  return (uint32_t)random(65536L) <= probability;
}
static uint8_t chooseRepeatCount(uint16_t macro) {
  const uint8_t pulls = 1u + (uint32_t)macro * 5u / GLITCH_Q16_MAX;
  uint8_t best = 0;
  for (uint8_t i = 0; i < pulls; ++i) best = max(best, (uint8_t)random(7L));
  return 2u + best;
}

static void resetAudioState(PlaybackState &state) {
  for (uint8_t i = 0; i < GLITCH_HISTORY_SLOTS; ++i) {
    history_valid[i] = false;
    history_length[i] = 0;
  }
  state = {};
  state.expected_beat_samples = SAMPLERATE / 2u;
  state.playback_repeat_count = 1;
  state.clock_mode = -1;
  state.clock_ratio = -1;
}

static void finalizeRecordedBeat(uint8_t slot, uint32_t length) {
  if (length >= GLITCH_MIN_CLOCK_FRAMES) {
    length = min(length, GLITCH_MAX_BEAT_FRAMES);
    history_length[slot] = (uint16_t)length;
    history_valid[slot] = true;
  } else {
    history_length[slot] = 0;
    history_valid[slot] = false;
  }
}

static void choosePlayback(PlaybackState &state, uint32_t beat_samples) {
  state.playback_active = false;
  state.playback_reverse = false;
  state.playback_repeat_count = 1;
  state.playback_length = 0;
  if (!chance(toQ16(active_break))) return;
  uint8_t slots[GLITCH_HISTORY_SLOTS];
  uint8_t count = 0;
  for (uint8_t i = 0; i < GLITCH_HISTORY_SLOTS; ++i) {
    if (i != state.record_slot && history_valid[i] && history_length[i] >= 2)
      slots[count++] = i;
  }
  if (!count) return;
  state.playback_slot = slots[random((long)count)];
  state.playback_length = history_length[state.playback_slot];
  state.playback_reverse = chance(toQ16(active_reverse));
  if (chance(toQ16(active_repeat)))
    state.playback_repeat_count = chooseRepeatCount(toQ16(active_repeat));
  state.expected_beat_samples = beat_samples;
  state.playback_active = true;
  uint8_t events = GLITCH_VISUAL_BREAK;
  if (state.playback_reverse) events |= GLITCH_VISUAL_REVERSE;
  if (state.playback_repeat_count > 1u) events |= GLITCH_VISUAL_REPEAT;
  __atomic_fetch_or(&glitch_visual_events, events, __ATOMIC_RELAXED);
}

static StereoFrame readSlice(uint8_t slot, uint16_t length,
                             uint32_t phase_q16, bool reverse) {
  StereoFrame zero = {0, 0};
  if (!history_valid[slot] || !length) return zero;
  const uint32_t max_phase = ((uint32_t)(length - 1u) << 16);
  phase_q16 = min(phase_q16, max_phase);
  const uint32_t phase = reverse ? max_phase - phase_q16 : phase_q16;
  const uint16_t index = phase >> 16;
  const uint16_t next = min((uint16_t)(index + 1u), (uint16_t)(length - 1u));
  const uint16_t frac = phase & 0xffffu;
  const StereoFrame a = history[slot][index];
  const StereoFrame b = history[slot][next];
  StereoFrame out;
  out.left = a.left + (int32_t)(((int64_t)(b.left - a.left) * frac + 32768) >> 16);
  out.right = a.right + (int32_t)(((int64_t)(b.right - a.right) * frac + 32768) >> 16);
  return out;
}

static uint16_t segmentEnvelope(uint32_t position, uint32_t length) {
  if (length < 4) return GLITCH_Q16_MAX;
  uint32_t fade = min(GLITCH_FADE_FRAMES, length >> 2);
  if (!fade) return GLITCH_Q16_MAX;
  const uint32_t distance = min(position, length - 1u - position);
  return distance >= fade ? GLITCH_Q16_MAX
                          : (uint32_t)distance * GLITCH_Q16_MAX / fade;
}

static StereoFrame renderPlayback(const PlaybackState &state) {
  StereoFrame zero = {0, 0};
  if (!state.playback_active || state.playback_length < 2) return zero;
  const uint32_t segment_length = max(1u, state.expected_beat_samples /
                                               state.playback_repeat_count);
  const uint32_t position = state.beat_pos % segment_length;
  uint32_t phase = 0;
  if (segment_length > 1) {
    phase = (uint32_t)((((uint64_t)position * (state.playback_length - 1u)) << 16) /
                       (segment_length - 1u));
  }
  StereoFrame out = readSlice(state.playback_slot, state.playback_length,
                              phase, state.playback_reverse);
  const uint16_t env = segmentEnvelope(position, segment_length);
  out.left = ((int32_t)out.left * env) >> 16;
  out.right = ((int32_t)out.right * env) >> 16;
  return out;
}

static void handleClockEdge(PlaybackState &state, uint32_t measured) {
  measured = max(measured, GLITCH_MIN_CLOCK_FRAMES);
  finalizeRecordedBeat(state.record_slot, state.record_pos);
  state.record_slot = (state.record_slot + 1u) % GLITCH_HISTORY_SLOTS;
  state.record_pos = 0;
  history_valid[state.record_slot] = false;
  history_length[state.record_slot] = 0;
  state.beat_pos = 0;
  state.samples_since_edge = 0;
  state.expected_beat_samples = measured;
  state.clock_seen = true;
  choosePlayback(state, measured);
}

static inline int32_t outputSample(int16_t sample, int16_t level, int16_t exit_gain) {
  int32_t value = (int64_t)sample * level * exit_gain / 1000000;
  value = constrain(value, -32768, 32767);
  return value << 16;
}

static void prepareGlitchExit() {
  glitch_exit_gain = 0;
  delay(32);
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
  loadGlitchState();
  syncGlitchTempoMenu();
  serviceGlitchControls();
  const uint32_t cv_seed =
      ((uint32_t)PicoCVInputReadRaw(0) << 16) ^ PicoCVInputReadRaw(1);
  randomSeed(cv_seed ^ micros());
  resetAudioState(audio_state);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) while (true) {}
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  displaytimer = millis();
  drawmenu(0);
  menuenc.getValue();
  glitch_audio_ready = true;
}

void loop() {
  static int16_t debounce_counter = 0;
  PicoProServiceSelectorExit(ENCSW_IN, menuenc, saveGlitchState, prepareGlitchExit);
  serviceGlitchControls();
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
  serviceGlitchAnimation();
  if ((millis() - displaytimer) > DISPLAY_BLANK_MS && UI_state == RUN) {
    UI_state = DORMANT;
    blankdisplay();
  }
}

void setup1() {
  while (!glitch_audio_ready) tight_loop_contents();
}

void loop1() {
  static int16_t exit_gain = 1000;
  const int32_t input_left_32 = i2s.read();
  const int32_t input_right_32 = i2s.read();
  StereoFrame input = {(int16_t)(input_left_32 >> 16),
                       (int16_t)(input_right_32 >> 16)};

  const int16_t clock_mode = active_clock;
  const int16_t clock_ratio = active_clock_ratio;
  if (clock_mode != audio_state.clock_mode ||
      (clock_mode != 0 && clock_ratio != audio_state.clock_ratio)) {
    // Do not mix slices recorded against different clock domains.
    resetAudioState(audio_state);
    audio_state.clock_mode = clock_mode;
    audio_state.clock_ratio = clock_ratio;
  }
  bool clock_edge = false;
  if (clock_mode == 0) {
    const uint32_t interval = (uint32_t)SAMPLERATE * 60u /
                              (uint32_t)constrain(active_bpm, 20, 200);
    if (audio_state.samples_since_edge >= interval) clock_edge = true;
  } else {
    const bool raw_clock =
        PicoCVInputDigitalActiveLow((uint8_t)(clock_mode - 1));
    if (raw_clock == audio_state.raw_clock_state) {
      if (audio_state.raw_clock_stable_samples < UINT32_MAX)
        ++audio_state.raw_clock_stable_samples;
    } else {
      audio_state.raw_clock_state = raw_clock;
      audio_state.raw_clock_stable_samples = 1;
    }
    if (audio_state.raw_clock_stable_samples >= GLITCH_CLOCK_DEBOUNCE_FRAMES &&
        audio_state.debounced_clock_state != audio_state.raw_clock_state) {
      const bool was_high = audio_state.debounced_clock_state;
      audio_state.debounced_clock_state = audio_state.raw_clock_state;
      clock_edge = audio_state.debounced_clock_state && !was_high;
    }

    if (clock_edge) {
      const uint32_t raw_period = audio_state.raw_samples_since_edge;
      audio_state.raw_samples_since_edge = 0;
      if (clock_ratio == 0) {
        clock_edge = ++audio_state.division_counter >= 2;
        if (clock_edge) audio_state.division_counter = 0;
      } else {
        const uint8_t multiplier = (uint8_t)clock_ratio;
        if (audio_state.raw_clock_seen && raw_period >= GLITCH_MIN_CLOCK_FRAMES) {
          audio_state.subdivision_samples = max(
              GLITCH_MIN_CLOCK_FRAMES, raw_period / multiplier);
          audio_state.subdivision_countdown = audio_state.subdivision_samples;
          audio_state.subdivisions_remaining = multiplier - 1u;
        } else {
          audio_state.subdivisions_remaining = 0;
        }
      }
      audio_state.raw_clock_seen = true;
    } else if (clock_ratio > 1 && audio_state.raw_clock_seen &&
               audio_state.subdivisions_remaining) {
      if (audio_state.subdivision_countdown > 0)
        --audio_state.subdivision_countdown;
      if (audio_state.subdivision_countdown == 0) {
        clock_edge = true;
        --audio_state.subdivisions_remaining;
        audio_state.subdivision_countdown = audio_state.subdivision_samples;
      }
    }
  }

  if (clock_edge) {
    uint32_t measured = (clock_mode == 0 || clock_ratio == 0 ||
                         audio_state.clock_seen)
                            ? audio_state.samples_since_edge
                            : audio_state.record_pos;
    handleClockEdge(audio_state, measured);
  }

  if (audio_state.record_pos < GLITCH_MAX_BEAT_FRAMES) {
    history[audio_state.record_slot][audio_state.record_pos++] = input;
  }
  StereoFrame output = input;
  if (audio_state.playback_active && audio_state.clock_seen)
    output = renderPlayback(audio_state);

  if (exit_gain > glitch_exit_gain) --exit_gain;
  const int16_t level = active_level;
  i2s.write(outputSample(output.left, level, exit_gain));
  i2s.write(outputSample(output.right, level, exit_gain));

  if (audio_state.samples_since_edge < UINT32_MAX) ++audio_state.samples_since_edge;
  if (clock_mode != 0 && audio_state.raw_samples_since_edge < UINT32_MAX)
    ++audio_state.raw_samples_since_edge;
  if (audio_state.beat_pos < UINT32_MAX) ++audio_state.beat_pos;
  if (clock_mode != 0 && audio_state.clock_seen &&
      audio_state.samples_since_edge > GLITCH_CLOCK_TIMEOUT_FRAMES) {
    audio_state.clock_seen = false;
    audio_state.playback_active = false;
    audio_state.playback_repeat_count = 1;
  }
}
