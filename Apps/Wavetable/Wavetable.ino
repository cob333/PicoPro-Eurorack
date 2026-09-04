/*
PicoPro Wavetable

Lightweight oscillator app:
- source WAV banks are converted offline into int16 flash tables
- audio core only performs phase accumulation and linear interpolation
- UI/core0 handles menu, OLED, persistence, and selector exit
*/

#include "PicoPro.h"
#include <I2S.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ClickEncoder.h"
#include "PicoAppState.h"
#include "ADSR.h"
#include "wavetables/wavetable_bank.h"

#ifdef PICOPRO_WAVETABLE_VARIABLE_MIP_SIZE
#define PICOPRO_WAVETABLE_TABLE(table_index, mip_index) PicoProWavetableTable((table_index), (mip_index))
#elif !defined(PICOPRO_WAVETABLE_MIP_COUNT)
#define PICOPRO_WAVETABLE_MIP_COUNT 1
#define PICOPRO_WAVETABLE_TABLE(table_index, mip_index) PICOPRO_WAVETABLES[(table_index)]
#else
#define PICOPRO_WAVETABLE_TABLE(table_index, mip_index) PICOPRO_WAVETABLES[(table_index)][(mip_index)]
#endif

#define WAVETABLE_SAMPLE_RATE 44100
#ifndef PICOPRO_WAVETABLE_MIP0_INDEX_SHIFT
#if PICOPRO_WAVETABLE_SIZE == 512
#define PICOPRO_WAVETABLE_MIP0_INDEX_SHIFT 23
#define PICOPRO_WAVETABLE_MIP0_FRAC_SHIFT 7
#define PICOPRO_WAVETABLE_MIP0_MASK 511
#elif PICOPRO_WAVETABLE_SIZE == 256
#define PICOPRO_WAVETABLE_MIP0_INDEX_SHIFT 24
#define PICOPRO_WAVETABLE_MIP0_FRAC_SHIFT 8
#define PICOPRO_WAVETABLE_MIP0_MASK 255
#else
#error "PICOPRO_WAVETABLE_SIZE must be 256 or 512"
#endif
#endif

#define WAVETABLE_STATE_TAG PICOPRO_APP_STATE_TAG('W', 'T', 'B', '5')
#define WAVETABLE_LEGACY_STATE_TAG PICOPRO_APP_STATE_TAG('W', 'T', 'B', '4')
#define WAVETABLE_VIEW_LAYERS 5
#define WAVETABLE_VIEW_POINTS 24
#define WAVETABLE_CROSSFADE_SAMPLES 128
#define WAVETABLE_CV_UPDATE_MS 2u

I2S i2s(OUTPUT);

enum UISTATES {RUN, DORMANT, WAIT_BUTTON_RELEASE};
enum MenuModes {PARAM_SELECT, PARAM_INPUT, WAITBUTTONRELEASE1, WAITBUTTONRELEASE2};
int16_t UI_state = RUN;

#define DEBOUNCE_CYCLES 100

int32_t displaytimer;

ClickEncoder menuenc(ENCA_IN, ENCB_IN, ENCSW_IN, ENCDIVIDE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#include "ui/CVModulation.h"

int16_t wavetablebank = 0;
int16_t wavetablewave = 0;
int16_t wavetablefreq = 90;
int16_t wavetablelevel = 100;
int16_t wavetable_adsr_gate = 0;
int16_t wavetable_adsr_attack = 0;
int16_t wavetable_adsr_decay = 15;
int16_t wavetable_adsr_sustain = 80;
int16_t wavetable_adsr_release = 10;

static uint32_t phase_accum = 0;
static volatile uint32_t phase_inc = 0;
static volatile uint16_t active_gain_q8 = 320;
static volatile int16_t active_level = 75;
static volatile int16_t target_level = 75;
static PicoADSRVoice wavetable_adsr_voice;
static uint8_t level_ramp_div = 0;
static const int16_t * volatile active_table = PICOPRO_WAVETABLE_TABLE(0, 0);
static const int16_t * volatile fade_from_table = PICOPRO_WAVETABLE_TABLE(0, 0);
static volatile uint8_t active_index_shift = PICOPRO_WAVETABLE_MIP0_INDEX_SHIFT;
static volatile uint8_t active_frac_shift = PICOPRO_WAVETABLE_MIP0_FRAC_SHIFT;
static volatile uint16_t active_index_mask = PICOPRO_WAVETABLE_MIP0_MASK;
static volatile uint8_t fade_index_shift = PICOPRO_WAVETABLE_MIP0_INDEX_SHIFT;
static volatile uint8_t fade_frac_shift = PICOPRO_WAVETABLE_MIP0_FRAC_SHIFT;
static volatile uint16_t fade_index_mask = PICOPRO_WAVETABLE_MIP0_MASK;
static volatile uint16_t crossfade_pos = WAVETABLE_CROSSFADE_SAMPLES;
static volatile uint16_t pending_audio_seq = 0;
static volatile uint16_t applied_audio_seq = 0;
static const int16_t * volatile pending_table = PICOPRO_WAVETABLE_TABLE(0, 0);
static volatile uint8_t pending_index_shift = PICOPRO_WAVETABLE_MIP0_INDEX_SHIFT;
static volatile uint8_t pending_frac_shift = PICOPRO_WAVETABLE_MIP0_FRAC_SHIFT;
static volatile uint16_t pending_index_mask = PICOPRO_WAVETABLE_MIP0_MASK;
static volatile uint16_t pending_gain_q8 = 320;
static volatile int16_t pending_level = 75;
static volatile uint32_t pending_phase_inc = 0;
static int16_t visual_bank = -1;
static int16_t visual_wave = -1;
static int16_t visual_menu = -1;
static int16_t visual_param = -32768;
static int16_t menustate = PARAM_SELECT;
static int8_t menuindex = 0;

struct WavetableState {
  int16_t bank;
  int16_t wave;
  int16_t freq;
  int16_t level;
  PicoADSRParams adsr;
  PicoCVPersistentState cv;
};

struct WavetableLegacyState {
  int16_t bank;
  int16_t wave;
  int16_t freq;
  int16_t level;
  PicoADSRParams adsr;
};

static void alarm_in_us_arm(uint32_t delay_us);
static void alarm_irq(void);
static void applyWavetableResolvedAudioParams(void);

static void alarm_in_us(uint32_t delay_us) {
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
  irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
  irq_set_enabled(ALARM_IRQ, true);
  alarm_in_us_arm(delay_us);
}

static void alarm_in_us_arm(uint32_t delay_us) {
  uint64_t target = timer_hw->timerawl + delay_us;
  timer_hw->alarm[ALARM_NUM] = (uint32_t) target;
}

static void alarm_irq(void) {
  menuenc.service();
  hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
  alarm_in_us_arm(TIMER_MICROS);
}

static inline int16_t wavetableOutputLevel(void) {
  int16_t level = active_level;
  if (++level_ramp_div >= 16) {
    level_ramp_div = 0;
    if (level > target_level) {
      --level;
      active_level = level;
    } else if (level < target_level) {
      ++level;
      active_level = level;
    }
  }
  return PicoADSRApplyAmp(&wavetable_adsr_voice, level);
}

static inline uint16_t wavetableLowFreqGainQ8(int16_t freq) {
  if (freq <= 55) {
    return 352;  // 1.375x
  }
  if (freq <= 110) {
    return 320;  // 1.25x
  }
  if (freq <= 220) {
    return 288;  // 1.125x
  }
  return 256;
}

static inline int32_t wavetableI2SClip(int32_t sample16) {
  if (sample16 > 32767) {
    sample16 = 32767;
  } else if (sample16 < -32768) {
    sample16 = -32768;
  }
  return sample16 << 16;
}

static inline uint8_t wavetableMipForFreq(int16_t freq) {
#if PICOPRO_WAVETABLE_MIP_COUNT <= 1
  (void)freq;
  return 0;
#else
  uint8_t mip = 0;
  if (freq > 172 && PICOPRO_WAVETABLE_MIP_COUNT > 1) mip = 1;
  if (freq > 344 && PICOPRO_WAVETABLE_MIP_COUNT > 2) mip = 2;
  if (freq > 689 && PICOPRO_WAVETABLE_MIP_COUNT > 3) mip = 3;
  if (freq > 1378 && PICOPRO_WAVETABLE_MIP_COUNT > 4) mip = 4;
  return mip;
#endif
}

static inline uint8_t wavetableIndexShiftForMip(uint8_t mip) {
#ifdef PICOPRO_WAVETABLE_VARIABLE_MIP_SIZE
  return PicoProWavetableMipIndexShift(mip);
#else
  (void)mip;
  return PICOPRO_WAVETABLE_MIP0_INDEX_SHIFT;
#endif
}

static inline uint8_t wavetableFracShiftForMip(uint8_t mip) {
#ifdef PICOPRO_WAVETABLE_VARIABLE_MIP_SIZE
  return PicoProWavetableMipFracShift(mip);
#else
  (void)mip;
  return PICOPRO_WAVETABLE_MIP0_FRAC_SHIFT;
#endif
}

static inline uint16_t wavetableMaskForMip(uint8_t mip) {
#ifdef PICOPRO_WAVETABLE_VARIABLE_MIP_SIZE
  return PicoProWavetableMipMask(mip);
#else
  (void)mip;
  return PICOPRO_WAVETABLE_MIP0_MASK;
#endif
}

static inline void postWavetableAudioParams(const int16_t *table,
                                            uint8_t mip,
                                            uint16_t gain_q8,
                                            int16_t level,
                                            uint32_t next_phase_inc) {
  uint16_t seq = pending_audio_seq + 1u;
  if ((seq & 1u) == 0) {
    ++seq;
  }
  pending_audio_seq = seq;
  pending_table = table;
  pending_index_shift = wavetableIndexShiftForMip(mip);
  pending_frac_shift = wavetableFracShiftForMip(mip);
  pending_index_mask = wavetableMaskForMip(mip);
  pending_gain_q8 = gain_q8;
  pending_level = level;
  pending_phase_inc = next_phase_inc;
  pending_audio_seq = seq + 1u;
}

static inline void setActiveWavetableFromAudioCore(const int16_t *table,
                                                   uint8_t index_shift,
                                                   uint8_t frac_shift,
                                                   uint16_t index_mask) {
  if (table == active_table &&
      index_shift == active_index_shift &&
      frac_shift == active_frac_shift &&
      index_mask == active_index_mask) {
    return;
  }
  fade_from_table = active_table;
  fade_index_shift = active_index_shift;
  fade_frac_shift = active_frac_shift;
  fade_index_mask = active_index_mask;
  active_table = table;
  active_index_shift = index_shift;
  active_frac_shift = frac_shift;
  active_index_mask = index_mask;
  crossfade_pos = 0;
}

static inline void serviceWavetableAudioUpdate(void) {
  const uint16_t seq_before = pending_audio_seq;
  if ((seq_before & 1u) || seq_before == applied_audio_seq) {
    return;
  }

  const int16_t *table = (const int16_t *)pending_table;
  const uint8_t index_shift = pending_index_shift;
  const uint8_t frac_shift = pending_frac_shift;
  const uint16_t index_mask = pending_index_mask;
  const uint16_t gain_q8 = pending_gain_q8;
  const int16_t level = pending_level;
  const uint32_t next_phase_inc = pending_phase_inc;

  if (seq_before != pending_audio_seq) {
    return;
  }

  setActiveWavetableFromAudioCore(table, index_shift, frac_shift, index_mask);
  active_gain_q8 = gain_q8;
  target_level = level;
  phase_inc = next_phase_inc;
  applied_audio_seq = seq_before;
}

static inline int32_t wavetableInterpolate(const int16_t *table,
                                           uint32_t phase,
                                           uint8_t index_shift,
                                           uint8_t frac_shift,
                                           uint16_t index_mask) {
  const uint16_t index = phase >> index_shift;
  const uint16_t frac = (phase >> frac_shift) & 0xffff;
  const int16_t a = table[index];
  const int16_t b = table[(index + 1) & index_mask];
  return (int32_t)a + (((int32_t)(b - a) * frac) >> 16);
}

static inline int32_t wavetableCrossfadedSample(const int16_t *table,
                                                uint32_t phase,
                                                uint8_t index_shift,
                                                uint8_t frac_shift,
                                                uint16_t index_mask) {
  const int32_t next = wavetableInterpolate(table, phase, index_shift, frac_shift, index_mask);
  uint16_t fade = crossfade_pos;
  if (fade >= WAVETABLE_CROSSFADE_SAMPLES) {
    return next;
  }

  const int32_t prev = wavetableInterpolate((const int16_t *)fade_from_table,
                                            phase,
                                            fade_index_shift,
                                            fade_frac_shift,
                                            fade_index_mask);
  const int32_t mixed =
      ((prev * (WAVETABLE_CROSSFADE_SAMPLES - fade)) + (next * fade)) /
      WAVETABLE_CROSSFADE_SAMPLES;
  crossfade_pos = fade + 1u;
  return mixed;
}

static void applyWavetableAudioParams(int16_t bank_index,
                                      int16_t wave_index,
                                      float frequency_hz,
                                      int16_t level) {
  const PicoProWavetableBank *bank = &PICOPRO_WAVETABLE_BANKS[bank_index];
  const int16_t display_freq = (int16_t)(frequency_hz + 0.5f);
  const uint8_t mip = wavetableMipForFreq(display_freq);
  const uint32_t next_phase_inc =
      (uint32_t)(((double)frequency_hz * 4294967296.0) /
                 (double)WAVETABLE_SAMPLE_RATE);
  postWavetableAudioParams(PICOPRO_WAVETABLE_TABLE(bank->start + wave_index, mip),
                           mip,
                           wavetableLowFreqGainQ8(display_freq),
                           level,
                           next_phase_inc);
}

static void refreshAudioParams(void) {
  if (wavetablebank < 0) {
    wavetablebank = 0;
  }
  if (wavetablebank >= PICOPRO_WAVETABLE_BANK_COUNT) {
    wavetablebank = PICOPRO_WAVETABLE_BANK_COUNT - 1;
  }

  const PicoProWavetableBank *bank = &PICOPRO_WAVETABLE_BANKS[wavetablebank];
  if (wavetablewave < 0) {
    wavetablewave = 0;
  }
  if (wavetablewave >= bank->count) {
    wavetablewave = bank->count - 1;
  }
  if (wavetablefreq < 20) {
    wavetablefreq = 20;
  }
  if (wavetablefreq > 2000) {
    wavetablefreq = 2000;
  }
  if (wavetablelevel < 0) {
    wavetablelevel = 0;
  }
  if (wavetablelevel > 100) {
    wavetablelevel = 100;
  }
  if (wavetable_adsr_gate < 0) {
    wavetable_adsr_gate = 0;
  }
  if (wavetable_adsr_gate > 2) {
    wavetable_adsr_gate = 2;
  }

  applyWavetableResolvedAudioParams();
}

static bool loadWavetableState(void) {
  WavetableState state;
  if (PicoAppStateLoad(WAVETABLE_STATE_TAG, &state, sizeof(state))) {
    wavetablebank = state.bank;
    wavetablewave = state.wave;
    wavetablefreq = state.freq;
    wavetablelevel = state.level;
    wavetable_adsr_gate = state.adsr.gate;
    wavetable_adsr_attack = state.adsr.attack;
    wavetable_adsr_decay = state.adsr.decay;
    wavetable_adsr_sustain = state.adsr.sustain;
    wavetable_adsr_release = state.adsr.release;
    PicoCVImportState(&state.cv);
    return true;
  }
  WavetableLegacyState legacy;
  if (PicoAppStateLoad(WAVETABLE_LEGACY_STATE_TAG, &legacy, sizeof(legacy))) {
    wavetablebank = legacy.bank;
    wavetablewave = legacy.wave;
    wavetablefreq = legacy.freq;
    wavetablelevel = legacy.level;
    wavetable_adsr_gate = legacy.adsr.gate;
    wavetable_adsr_attack = legacy.adsr.attack;
    wavetable_adsr_decay = legacy.adsr.decay;
    wavetable_adsr_sustain = legacy.adsr.sustain;
    wavetable_adsr_release = legacy.adsr.release;
  }
  return false;
}

static void saveWavetableState(void) {
  WavetableState state = {
    wavetablebank,
    wavetablewave,
    wavetablefreq,
    wavetablelevel,
    {wavetable_adsr_gate,
     wavetable_adsr_attack,
     wavetable_adsr_decay,
     wavetable_adsr_sustain,
     wavetable_adsr_release},
  };
  PicoCVExportState(&state.cv);
  PicoAppStateSave(WAVETABLE_STATE_TAG, &state, sizeof(state));
}

static void prepareWavetableExit(void) {
  target_level = 0;
  delay(32);
}

void updateWavetableMenu(int16_t value, int16_t aux) {
  (void)value;
  (void)aux;
  refreshAudioParams();
}

#include "Wavetable_Menu.h"
#include "ui/KnobDisplay.h"

#define NUM_MENUS (sizeof(menus) / sizeof(menu))

static const PicoADSRMenuBinding wavetableADSRMenus[] = {
  {&wavetable_adsr_gate,
   &wavetable_adsr_attack,
   &wavetable_adsr_decay,
   &wavetable_adsr_sustain,
   &wavetable_adsr_release}
};

static void updatedisplay(void) {
  display.display();
  displaytimer = millis();
}

static void blankdisplay(void) {
  display.clearDisplay();
  display.display();
}

static int16_t currentMenuMax(int8_t index) {
  if (menus[index].parameter == &wavetablewave) {
    const PicoProWavetableBank *bank = &PICOPRO_WAVETABLE_BANKS[wavetablebank];
    return bank->count > 0 ? bank->count - 1 : 0;
  }
  return menus[index].max;
}

static void applyWavetableResolvedAudioParams(void) {
  static int16_t last_bank = -1;
  static int16_t last_wave = -1;
  static float last_frequency_hz = -1.0f;
  static int16_t last_level = -1;
  static int16_t last_gate = -1;
  static int16_t last_attack = -1;
  static int16_t last_decay = -1;
  static int16_t last_sustain = -1;
  static int16_t last_release = -1;

  const int16_t bank = PicoCVModulatedValue(0, wavetablebank, menus[0].min, menus[0].max);
  const PicoProWavetableBank *bank_info = &PICOPRO_WAVETABLE_BANKS[
      bank < 0 ? 0 : (bank >= PICOPRO_WAVETABLE_BANK_COUNT ? PICOPRO_WAVETABLE_BANK_COUNT - 1 : bank)];
  const int16_t wave_max = bank_info->count > 0 ? bank_info->count - 1 : 0;
  const int16_t wave = PicoCVModulatedValue(1, wavetablewave, menus[1].min, wave_max);
  const float frequency_hz = PicoCVModulatedFrequencyHz(
      2, (float)wavetablefreq, (float)menus[2].min, (float)menus[2].max);
  const int16_t level = PicoCVModulatedValue(3, wavetablelevel, menus[3].min, menus[3].max);
  const int16_t gate = wavetable_adsr_gate;
  const int16_t attack = PicoCVModulatedValue(5, wavetable_adsr_attack, menus[5].min, menus[5].max);
  const int16_t decay = PicoCVModulatedValue(6, wavetable_adsr_decay, menus[6].min, menus[6].max);
  const int16_t sustain = PicoCVModulatedValue(7, wavetable_adsr_sustain, menus[7].min, menus[7].max);
  const int16_t release = PicoCVModulatedValue(8, wavetable_adsr_release, menus[8].min, menus[8].max);

  if (bank != last_bank ||
      wave != last_wave ||
      frequency_hz != last_frequency_hz ||
      level != last_level) {
    applyWavetableAudioParams(bank, wave, frequency_hz, level);
    last_bank = bank;
    last_wave = wave;
    last_frequency_hz = frequency_hz;
    last_level = level;
  }

  if (gate != last_gate ||
      attack != last_attack ||
      decay != last_decay ||
      sustain != last_sustain ||
      release != last_release) {
    PicoADSRApplyParams(&wavetable_adsr_voice,
                        gate,
                        attack,
                        decay,
                        sustain,
                        release,
                        WAVETABLE_SAMPLE_RATE);
    last_gate = gate;
    last_attack = attack;
    last_decay = decay;
    last_sustain = sustain;
    last_release = release;
  }
}

static void serviceWavetableCV(void) {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if ((now - last_ms) < WAVETABLE_CV_UPDATE_MS) {
    return;
  }
  last_ms = now;
  applyWavetableResolvedAudioParams();
}

static void drawTextCentered(const char *text, int16_t y) {
  int16_t len = 0;
  while (text[len] != 0 && len < 10) {
    ++len;
  }
  const int16_t x = (SCREEN_WIDTH - len * 6) / 2;
  display.setCursor(x, y);
  display.write((const uint8_t *)text, len);
}

static int16_t clampTableIndex(int16_t wave, uint16_t count) {
  if (wave < 0) {
    return 0;
  }
  if (wave >= count) {
    return count - 1;
  }
  return wave;
}

static int16_t projectWaveY(int16_t sample, int16_t base_y) {
  int16_t y = base_y - (sample / 8192);
  if (y < 0) {
    return 0;
  }
  if (y > 19) {
    return 19;
  }
  return y;
}

static void drawWavetableView(void) {
  int16_t bank_index = PicoCVDisplayValue(0, wavetablebank);
  if (bank_index < 0) {
    bank_index = 0;
  }
  if (bank_index >= PICOPRO_WAVETABLE_BANK_COUNT) {
    bank_index = PICOPRO_WAVETABLE_BANK_COUNT - 1;
  }
  const PicoProWavetableBank *bank = &PICOPRO_WAVETABLE_BANKS[bank_index];
  const int16_t center_wave = PicoCVDisplayValue(1, wavetablewave);
  int16_t first_y[WAVETABLE_VIEW_LAYERS];
  int16_t last_y[WAVETABLE_VIEW_LAYERS];
  int16_t first_x[WAVETABLE_VIEW_LAYERS];
  int16_t last_x[WAVETABLE_VIEW_LAYERS];

  for (int8_t layer = 0; layer < WAVETABLE_VIEW_LAYERS; ++layer) {
    const int16_t rel = layer - (WAVETABLE_VIEW_LAYERS / 2);
    const int16_t wave = clampTableIndex(center_wave + rel, bank->count);
    const int16_t *table = PICOPRO_WAVETABLE_TABLE(bank->start + wave, 0);
    const int16_t depth = WAVETABLE_VIEW_LAYERS - 1 - layer;
    const int16_t x_offset = depth * 2;
    const int16_t base_y = 4 + layer * 3;
    int16_t prev_x = 0;
    int16_t prev_y = 0;

    for (int8_t point = 0; point < WAVETABLE_VIEW_POINTS; ++point) {
      const uint16_t sample_index =
          ((uint16_t)point * PICOPRO_WAVETABLE_SIZE) / WAVETABLE_VIEW_POINTS;
      const int16_t x = 5 + x_offset + point * 2;
      const int16_t y = projectWaveY(table[sample_index], base_y);

      if (point == 0) {
        first_x[layer] = x;
        first_y[layer] = y;
      } else {
        if (rel == 0) {
          display.drawLine(prev_x, prev_y, x, y, WHITE);
          display.drawLine(prev_x, prev_y + 1, x, y + 1, WHITE);
        } else if ((point & 1) != 0) {
          // Alternate line segments so unselected waves remain visible but do
          // not compete with the solid, double-weight selected waveform.
          display.drawLine(prev_x, prev_y, x, y, WHITE);
        }
      }
      if (point == WAVETABLE_VIEW_POINTS - 1) {
        last_x[layer] = x;
        last_y[layer] = y;
      }
      prev_x = x;
      prev_y = y;
    }
  }

  for (int8_t layer = 1; layer < WAVETABLE_VIEW_LAYERS; ++layer) {
    display.drawLine(first_x[layer - 1], first_y[layer - 1],
                     first_x[layer], first_y[layer], WHITE);
    display.drawLine(last_x[layer - 1], last_y[layer - 1],
                     last_x[layer], last_y[layer], WHITE);
  }
}

static void formatMenuValue(char *text, size_t len, int8_t index) {
  if (menus[index].parameter == &wavetablebank) {
    snprintf(text, len, "%s", PICOPRO_WAVETABLE_BANK_NAMES[wavetablebank]);
  } else if (menus[index].parameter == &wavetablewave) {
    snprintf(text, len, "wav %d", wavetablewave);
  } else if (menus[index].parameter == &wavetablefreq) {
    snprintf(text, len, "%dhz", wavetablefreq);
  } else if (menus[index].parameter == &wavetablelevel) {
    snprintf(text, len, "lvl %d", wavetablelevel);
  } else if (menus[index].parameter == &wavetable_adsr_gate) {
    snprintf(text, len, "%s", textgate[wavetable_adsr_gate]);
  } else if (menus[index].parameter == &wavetable_adsr_attack) {
    snprintf(text, len, "atk %d", wavetable_adsr_attack);
  } else if (menus[index].parameter == &wavetable_adsr_decay) {
    snprintf(text, len, "dec %d", wavetable_adsr_decay);
  } else if (menus[index].parameter == &wavetable_adsr_sustain) {
    snprintf(text, len, "sus %d", wavetable_adsr_sustain);
  } else if (menus[index].parameter == &wavetable_adsr_release) {
    snprintf(text, len, "rel %d", wavetable_adsr_release);
  } else {
    snprintf(text, len, "%d", *menus[index].parameter);
  }
}

static bool isWaveSelectPage(void) {
  return menus[menuindex].parameter == &wavetablewave;
}

static void drawStandardMenuScreen(bool editing, int8_t nav_dir = 0) {
  if (!editing && nav_dir != 0) {
    PicoKnobDrawMenuItem(&menus[menuindex], false, nav_dir, menuindex, NUM_MENUS);
    updatedisplay();
    delay(PICOPRO_KNOB_NAV_ANIM_MS);
  }
  PicoKnobDrawMenuItem(&menus[menuindex], editing, 0, menuindex, NUM_MENUS);
  updatedisplay();
}

static void drawWavetableScreen(bool editing, int8_t nav_dir = 0) {
  char text[9];

  if (!isWaveSelectPage()) {
    drawStandardMenuScreen(editing, nav_dir);
    visual_bank = wavetablebank;
    visual_wave = wavetablewave;
    visual_menu = menuindex;
    visual_param = *menus[menuindex].parameter;
    return;
  }

  display.clearDisplay();
  drawWavetableView();
  if (editing) {
    display.fillRect(0, 23, SCREEN_WIDTH, 9, WHITE);
  }
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(editing ? BLACK : WHITE, editing ? WHITE : BLACK);
  formatMenuValue(text, sizeof(text), menuindex);
  drawTextCentered(text, 24);
  if (editing) {
    display.setTextColor(WHITE, BLACK);
  }
  updatedisplay();
  visual_bank = wavetablebank;
  visual_wave = wavetablewave;
  visual_menu = menuindex;
  visual_param = *menus[menuindex].parameter;
}

static void drawWavetableScreenIfDirty(void) {
  const uint32_t now_ms = millis();
  bool cv_dirty = PicoADSRShouldRefreshEnvelopeForMenu(&menus[menuindex], now_ms);
  if (!cv_dirty) {
    if (isWaveSelectPage()) {
      const uint8_t wave_indices[] = {0, 1};
      cv_dirty = PicoCVShouldRefreshDisplayGroup(wave_indices, 2, now_ms);
    } else {
      cv_dirty = PicoCVShouldRefreshDisplay(menuindex, now_ms);
    }
  }
  if (visual_bank != wavetablebank ||
      visual_wave != wavetablewave ||
      visual_menu != menuindex ||
      visual_param != *menus[menuindex].parameter ||
      cv_dirty) {
    drawWavetableScreen(menustate == PARAM_INPUT);
  }
}

static inline int16_t scaledMenuInput(int16_t enc) {
  if (enc == 0 || !menuenc.isPressRotateLocked()) {
    return enc;
  }
  return enc * menuenc.getPressRotateMultiplier();
}

static inline bool applyMenuInput(int16_t enc) {
  const int16_t scaled = scaledMenuInput(enc);
  if (scaled == 0) {
    return false;
  }
  int32_t temp = *menus[menuindex].parameter + (int32_t)scaled * menus[menuindex].step;
  const int16_t max_value = currentMenuMax(menuindex);
  if (temp < menus[menuindex].min) {
    temp = menus[menuindex].min;
  }
  if (temp > max_value) {
    temp = max_value;
  }
  if (temp == *menus[menuindex].parameter) {
    return false;
  }
  *menus[menuindex].parameter = (int16_t)temp;
  if (menus[menuindex].handler != 0) {
    (*menus[menuindex].handler)((int16_t)temp, menus[menuindex].parameter2);
  }
  return true;
}

static void domenus(void) {
  int16_t enc = menuenc.getValue();
  static int16_t debouncecounter;
  const bool button_down = !digitalRead(ENCSW_IN);
  const ClickEncoder::Button button = menuenc.getButton();

  const uint8_t cv_result = PicoCVServiceOverlay(
      menus, NUM_MENUS, enc, button_down, button);
  if (cv_result != 0) {
    if (cv_result == 2) {
      drawWavetableScreen(menustate == PARAM_INPUT);
    }
    return;
  }

  if (PicoCVHandleEntryButton(button, menuindex)) {
    PicoCVDrawOverlay(&menus[menuindex]);
    return;
  }

  switch (menustate) {
    case PARAM_SELECT:
      if (enc != 0) {
        menuindex += enc;
        if (menuindex < 0) {
          menuindex = NUM_MENUS - 1;
        }
        if (menuindex > (NUM_MENUS - 1)) {
          menuindex = 0;
        }
        drawWavetableScreen(false, enc);
      }
      if (button_down) {
        menustate = WAITBUTTONRELEASE1;
        debouncecounter = DEBOUNCE_CYCLES;
        drawWavetableScreen(true);
      }
      break;
    case WAITBUTTONRELEASE1:
      if (enc != 0 || menuenc.isPressRotateLocked()) {
        menustate = PARAM_INPUT;
        debouncecounter = DEBOUNCE_CYCLES;
        if (enc != 0) {
          applyMenuInput(enc);
        }
        drawWavetableScreen(true);
        break;
      }
      if (!button_down) {
        --debouncecounter;
        if (debouncecounter == 0) {
          menustate = PARAM_INPUT;
        }
      }
      break;
    case PARAM_INPUT:
      if (applyMenuInput(enc)) {
        drawWavetableScreen(true);
      }
      if (button_down && !menuenc.isPressRotateLocked()) {
        debouncecounter = DEBOUNCE_CYCLES;
        menustate = WAITBUTTONRELEASE2;
      }
      break;
    case WAITBUTTONRELEASE2:
      if (enc != 0 || menuenc.isPressRotateLocked()) {
        menustate = PARAM_INPUT;
        debouncecounter = DEBOUNCE_CYCLES;
        if (enc != 0) {
          applyMenuInput(enc);
        }
        drawWavetableScreen(true);
        break;
      }
      if (!button_down) {
        --debouncecounter;
        if (debouncecounter <= 0) {
          menustate = PARAM_SELECT;
          drawWavetableScreen(false);
        }
      }
      break;
    default:
      menustate = PARAM_SELECT;
      break;
  }
  drawWavetableScreenIfDirty();
}

void setup() {
  pinMode(ENCA_IN, INPUT_PULLUP);
  pinMode(ENCB_IN, INPUT_PULLUP);
  pinMode(ENCSW_IN, INPUT_PULLUP);

  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();

  alarm_in_us(TIMER_MICROS);
  PicoCVInputBegin();

  i2s.setDOUT(I2S_DATA);
  i2s.setBCLK(BCLK);
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(WAVETABLE_SAMPLE_RATE);
  i2s.begin();

  PicoADSRInitVoice(&wavetable_adsr_voice);
  PicoCVBindMenus(menus, NUM_MENUS);
  const bool cv_state_loaded = loadWavetableState();
  if (!cv_state_loaded) {
    PicoCVCommitAssignment(2, 1, PICOPRO_CV_VOCT_AMOUNT);
  }
  refreshAudioParams();

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    while(1);
  }
  display.clearDisplay();
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  displaytimer = millis();
  PicoADSRRegisterMenus(wavetableADSRMenus, 1);
  drawWavetableScreen(false);
  menuenc.getValue();
}

void loop() {
  static int16_t debouncecounter;
  int16_t encvalue;

  PicoProServiceSelectorExit(ENCSW_IN, menuenc, saveWavetableState, prepareWavetableExit);
  PicoADSRServiceGate(&wavetable_adsr_voice, millis());
  serviceWavetableCV();

  if ((millis() - displaytimer) > DISPLAY_BLANK_MS) {
    UI_state = DORMANT;
    blankdisplay();
  }

  switch (UI_state) {
    case RUN:
      domenus();
      break;
    case DORMANT:
      encvalue = menuenc.getValue();
      if (encvalue || !digitalRead(ENCSW_IN)) {
        drawWavetableScreen(false);
        debouncecounter = DEBOUNCE_CYCLES;
        UI_state = WAIT_BUTTON_RELEASE;
      }
      break;
    case WAIT_BUTTON_RELEASE:
      if (digitalRead(ENCSW_IN)) {
        --debouncecounter;
        if (debouncecounter == 0) {
          UI_state = RUN;
        }
      }
      break;
    default:
      UI_state = RUN;
      break;
  }
}

void setup1() {
  delay(1000);
}

void loop1() {
  serviceWavetableAudioUpdate();
  const int16_t *table = (const int16_t *)active_table;
  const uint32_t phase = phase_accum;
  const uint8_t index_shift = active_index_shift;
  const uint8_t frac_shift = active_frac_shift;
  const uint16_t index_mask = active_index_mask;
  const int32_t interp = wavetableCrossfadedSample(table, phase, index_shift, frac_shift, index_mask);
  const int16_t level = wavetableOutputLevel();
  int32_t sample16 = (interp * level) / 100;
  sample16 = (sample16 * active_gain_q8) >> 8;
  const int32_t sample = wavetableI2SClip(sample16);

  phase_accum = phase + phase_inc;
  i2s.write(sample);
  i2s.write(sample);
}
