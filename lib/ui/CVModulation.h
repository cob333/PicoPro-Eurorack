#ifndef PICOPRO_CV_MODULATION_H_
#define PICOPRO_CV_MODULATION_H_

#include <Arduino.h>
#include "../MenuTypes.h"
#include "../ClickEncoder.h"
#include "../CVPersistence.h"

#ifndef PICOPRO_CV_MAX_MENUS
#define PICOPRO_CV_MAX_MENUS 16
#endif

#ifndef PICOPRO_CV_DISPLAY_INTERVAL_MS
#define PICOPRO_CV_DISPLAY_INTERVAL_MS 67
#endif

#define PICOPRO_CV_INVALID_INDEX 0xffu
#define PICOPRO_CV_VOCT_AMOUNT 11
#define PICOPRO_CV_INPUT_LFO 2u
#define PICOPRO_CV_LFO_WAVE_COUNT 6u
#define PICOPRO_CV_LFO_FREQ_STEPS 64u
#define PICOPRO_CV_LFO_DEFAULT_FREQ_CODE 24u
#define PICOPRO_CV_ZERO_DEADBAND_VOLTS 0.015f
#ifndef PICOPRO_CV_NEGATIVE_FULL_SCALE_VOLTS
#define PICOPRO_CV_NEGATIVE_FULL_SCALE_VOLTS 1.4f
#endif
#ifndef PICOPRO_CV_POSITIVE_FULL_SCALE_VOLTS
#define PICOPRO_CV_POSITIVE_FULL_SCALE_VOLTS 5.0f
#endif
// Measured converter jitter is about one count. Holding across two counts
// rejects that noise with one count of margin while preserving V/oct detail.
// The voltage threshold is derived from each input's calibration below.
#define PICOPRO_CV_HYSTERESIS_COUNTS 2.0f
#define PICOPRO_CV_FILTER_TIME_MS 8.0f
#define PICOPRO_CV_FILTER_SNAP_COUNTS 32.0f

enum PicoCVUiState {
  PICOPRO_CV_UI_OFF = 0,
  PICOPRO_CV_UI_SELECT_INPUT,
  PICOPRO_CV_UI_LFO_WAVE,
  PICOPRO_CV_UI_LFO_FREQ,
  PICOPRO_CV_UI_AMOUNT,
  PICOPRO_CV_UI_WAIT_RELEASE_DONE
};

struct PicoCVAssignment {
  uint8_t input;
  int8_t amount;
  float baseline_volts;
  bool baseline_ready;
  uint8_t lfo_wave;
  uint8_t lfo_freq_code;
  float lfo_frequency_hz;
  float lfo_phase;
  float lfo_random_from;
  float lfo_random_to;
  float lfo_random_step;
  uint32_t lfo_random_state;
  uint32_t lfo_last_us;
  bool lfo_ready;
};

static PicoCVAssignment picoCVAssignments[PICOPRO_CV_MAX_MENUS];
static uint8_t picoCVUiState = PICOPRO_CV_UI_OFF;
static uint8_t picoCVEditIndex = 0;
static uint8_t picoCVSelectedInput = 0;
static int8_t picoCVEditAmount = 0;
static uint8_t picoCVEditLfoWave = 0;
static uint8_t picoCVEditLfoFreqCode = PICOPRO_CV_LFO_DEFAULT_FREQ_CODE;
static bool picoCVButtonWasDown = false;
static const menu *picoCVMenus = 0;
static uint8_t picoCVMenuCount = 0;
static int16_t picoCVDisplayValues[PICOPRO_CV_MAX_MENUS];
static bool picoCVDisplayReady[PICOPRO_CV_MAX_MENUS];
static bool picoCVDisplayDirty[PICOPRO_CV_MAX_MENUS];
static uint32_t picoCVLastDisplayMs = 0;
static PicoBootCalibration picoCVCalibration;
static bool picoCVCalibrationLoaded = false;
static float picoCVStableVolts[2];
static float picoCVFilteredRaw[2];
static uint32_t picoCVStableMs[2];
static bool picoCVStableReady[2];
static bool picoCVAssignmentsInitialized = false;

static const char *picoCVLfoWaveNames[PICOPRO_CV_LFO_WAVE_COUNT] = {
  "sin", "saw", "tri", "sqr", "rnd", "step"
};

static inline bool PicoCVIndexIsFrequency(uint8_t index);

static inline uint8_t PicoCVClampIndex(uint8_t index) {
  return index < PICOPRO_CV_MAX_MENUS ? index : (PICOPRO_CV_MAX_MENUS - 1);
}

static inline float PicoCVLfoFrequencyHz(uint8_t code) {
  code = min(code, (uint8_t)(PICOPRO_CV_LFO_FREQ_STEPS - 1u));
  // 64 logarithmic steps from 0.05 Hz to 20 Hz.
  return 0.05f * powf(400.0f,
                      (float)code / (float)(PICOPRO_CV_LFO_FREQ_STEPS - 1u));
}

static inline int8_t PicoCVActiveInput(uint8_t index) {
  index = PicoCVClampIndex(index);
  return picoCVAssignments[index].amount == 0 ? -1 : (int8_t)picoCVAssignments[index].input;
}

static inline void PicoCVBindMenus(const menu *menus, uint8_t count) {
  picoCVMenus = menus;
  picoCVMenuCount = count > PICOPRO_CV_MAX_MENUS ? PICOPRO_CV_MAX_MENUS : count;
  if (!picoCVAssignmentsInitialized) {
    for (uint8_t i = 0; i < PICOPRO_CV_MAX_MENUS; ++i) {
      picoCVAssignments[i].lfo_freq_code = PICOPRO_CV_LFO_DEFAULT_FREQ_CODE;
      picoCVAssignments[i].lfo_frequency_hz =
          PicoCVLfoFrequencyHz(PICOPRO_CV_LFO_DEFAULT_FREQ_CODE);
      picoCVAssignments[i].lfo_random_state = 0x6d2b79f5u ^
                                               ((uint32_t)i * 0x9e3779b9u);
    }
    picoCVAssignmentsInitialized = true;
  }
  // V/oct is a capability of frequency parameters, not a generic amount.
  // Sanitize persisted/legacy state after menu metadata becomes available.
  for (uint8_t i = 0; i < picoCVMenuCount; ++i) {
    if (picoCVAssignments[i].amount == PICOPRO_CV_VOCT_AMOUNT &&
        !PicoCVIndexIsFrequency(i)) {
      picoCVAssignments[i].amount = 0;
      picoCVAssignments[i].baseline_ready = false;
    }
  }
}

static inline void PicoCVExportState(PicoCVPersistentState *state) {
  if (state == nullptr) return;
  state->version = PICOPRO_CV_STATE_VERSION;
  state->count = PICOPRO_CV_MAX_MENUS;
  for (uint8_t i = 0; i < PICOPRO_CV_MAX_MENUS; ++i) {
    const PicoCVAssignment *assignment = &picoCVAssignments[i];
    const uint8_t amount_code = (uint8_t)constrain(
        (int16_t)assignment->amount + 10, 0, 21);
    const uint16_t packed = amount_code |
                            ((uint16_t)min(assignment->input,
                                          (uint8_t)PICOPRO_CV_INPUT_LFO) << 5) |
                            ((uint16_t)min(assignment->lfo_wave,
                                          (uint8_t)(PICOPRO_CV_LFO_WAVE_COUNT - 1u)) << 7) |
                            ((uint16_t)min(assignment->lfo_freq_code,
                                          (uint8_t)(PICOPRO_CV_LFO_FREQ_STEPS - 1u)) << 10);
    state->assignments[i].input = packed & 0xffu;
    state->assignments[i].amount = (int8_t)(packed >> 8);
  }
}

static inline void PicoCVImportState(const PicoCVPersistentState *state) {
  if (state == nullptr ||
      (state->version != PICOPRO_CV_STATE_VERSION &&
       state->version != PICOPRO_CV_LEGACY_STATE_VERSION)) return;
  const uint8_t count = state->count > PICOPRO_CV_MAX_MENUS
                            ? PICOPRO_CV_MAX_MENUS
                            : state->count;
  for (uint8_t i = 0; i < count; ++i) {
    PicoCVAssignment *assignment = &picoCVAssignments[i];
    if (state->version == PICOPRO_CV_LEGACY_STATE_VERSION) {
      const int8_t amount = state->assignments[i].amount;
      assignment->input = state->assignments[i].input == 0 ? 0 : 1;
      assignment->amount = amount >= -10 && amount <= PICOPRO_CV_VOCT_AMOUNT
                               ? amount
                               : 0;
      assignment->lfo_wave = 0;
      assignment->lfo_freq_code = PICOPRO_CV_LFO_DEFAULT_FREQ_CODE;
    } else {
      const uint16_t packed = state->assignments[i].input |
                              ((uint16_t)(uint8_t)state->assignments[i].amount << 8);
      const uint8_t amount_code = packed & 0x1fu;
      assignment->input = min((uint8_t)((packed >> 5) & 0x03u),
                              (uint8_t)PICOPRO_CV_INPUT_LFO);
      assignment->amount = amount_code <= 21u ? (int8_t)amount_code - 10 : 0;
      assignment->lfo_wave = min((uint8_t)((packed >> 7) & 0x07u),
                                 (uint8_t)(PICOPRO_CV_LFO_WAVE_COUNT - 1u));
      assignment->lfo_freq_code = min((uint8_t)((packed >> 10) & 0x3fu),
                                      (uint8_t)(PICOPRO_CV_LFO_FREQ_STEPS - 1u));
      if (assignment->input == PICOPRO_CV_INPUT_LFO &&
          assignment->amount == PICOPRO_CV_VOCT_AMOUNT) {
        assignment->amount = 10;
      }
    }
    // V/oct is absolute: zero volts always maps to the parameter's base value.
    assignment->baseline_volts = 0.0f;
    assignment->baseline_ready = false;
    assignment->lfo_phase = 0.0f;
    assignment->lfo_frequency_hz =
        PicoCVLfoFrequencyHz(assignment->lfo_freq_code);
    assignment->lfo_random_state = 0x6d2b79f5u ^
                                   ((uint32_t)i * 0x9e3779b9u);
    assignment->lfo_last_us = 0;
    assignment->lfo_ready = false;
    picoCVDisplayReady[i] = false;
    picoCVDisplayDirty[i] = true;
  }
}

static inline bool PicoCVNameEquals(const char *name, const char *target) {
  uint8_t i = 0;
  while (name[i] != 0 && target[i] != 0) {
    char a = name[i];
    char b = target[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
    if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
    if (a != b) return false;
    ++i;
  }
  return name[i] == 0 && target[i] == 0;
}

static inline bool PicoCVIndexIsFrequency(uint8_t index) {
  if (picoCVMenus == 0 || index >= picoCVMenuCount) {
    return false;
  }
  const char *name = picoCVMenus[index].name;
  return PicoCVNameEquals(name, "freq") || PicoCVNameEquals(name, "frequency");
}

static inline void PicoCVLoadCalibrationOnce(void) {
  if (!picoCVCalibrationLoaded) {
    PicoBootLoadCalibration(&picoCVCalibration);
    picoCVCalibrationLoaded = true;
  }
}

static inline void PicoCVStoreDisplayValue(uint8_t index, int16_t value, bool active) {
  index = PicoCVClampIndex(index);
  if (!picoCVDisplayReady[index] || picoCVDisplayValues[index] != value) {
    picoCVDisplayValues[index] = value;
    picoCVDisplayReady[index] = true;
    if (active) {
      picoCVDisplayDirty[index] = true;
    }
  }
}

static inline int16_t PicoCVDisplayValue(uint8_t index, int16_t base) {
  index = PicoCVClampIndex(index);
  if (picoCVAssignments[index].amount == 0 || !picoCVDisplayReady[index]) {
    return base;
  }
  return picoCVDisplayValues[index];
}

static inline int16_t PicoCVDisplayValueByIndex(uint8_t index, int16_t base) {
  return index == PICOPRO_CV_INVALID_INDEX ? base : PicoCVDisplayValue(index, base);
}

static inline bool PicoCVDisplayRefreshDue(uint32_t now_ms) {
  return picoCVUiState == PICOPRO_CV_UI_OFF &&
         (now_ms - picoCVLastDisplayMs) >= PICOPRO_CV_DISPLAY_INTERVAL_MS;
}

static inline bool PicoCVConsumeDisplayDirty(uint8_t index) {
  if (index == PICOPRO_CV_INVALID_INDEX) {
    return false;
  }
  index = PicoCVClampIndex(index);
  if (picoCVAssignments[index].amount == 0 || !picoCVDisplayDirty[index]) {
    return false;
  }
  picoCVDisplayDirty[index] = false;
  return true;
}

static inline bool PicoCVShouldRefreshDisplay(uint8_t index, uint32_t now_ms) {
  if (!PicoCVDisplayRefreshDue(now_ms) || !PicoCVConsumeDisplayDirty(index)) {
    return false;
  }
  picoCVLastDisplayMs = now_ms;
  return true;
}

static inline bool PicoCVShouldRefreshDisplayGroup(const uint8_t *indices,
                                                   uint8_t count,
                                                   uint32_t now_ms) {
  if (!PicoCVDisplayRefreshDue(now_ms)) {
    return false;
  }
  bool dirty = false;
  for (uint8_t i = 0; i < count; ++i) {
    dirty |= PicoCVConsumeDisplayDirty(indices[i]);
  }
  if (dirty) {
    picoCVLastDisplayMs = now_ms;
  }
  return dirty;
}

static inline void PicoCVBegin(uint8_t index) {
  picoCVEditIndex = PicoCVClampIndex(index);
  PicoCVAssignment *assignment = &picoCVAssignments[picoCVEditIndex];
  picoCVSelectedInput = min(assignment->input,
                            (uint8_t)PICOPRO_CV_INPUT_LFO);
  picoCVEditAmount = assignment->amount;
  picoCVEditLfoWave = min(assignment->lfo_wave,
                          (uint8_t)(PICOPRO_CV_LFO_WAVE_COUNT - 1u));
  picoCVEditLfoFreqCode = min(
      assignment->lfo_freq_code,
      (uint8_t)(PICOPRO_CV_LFO_FREQ_STEPS - 1u));
  picoCVUiState = PICOPRO_CV_UI_SELECT_INPUT;
  picoCVButtonWasDown = true;
}

static inline bool PicoCVHandleEntryButton(ClickEncoder::Button button, uint8_t index) {
  if (button == ClickEncoder::DoubleClicked) {
    PicoCVBegin(index);
    return true;
  }
  return false;
}

static inline int16_t PicoCVClampValue(int32_t value, int16_t min_value, int16_t max_value) {
  if (value < min_value) return min_value;
  if (value > max_value) return max_value;
  return (int16_t)value;
}

static inline uint16_t PicoCVReadRaw(uint8_t input) {
  input = input == 0 ? 0 : 1;
  return PicoCVInputReadRaw(input);
}

static inline float PicoCVRawToVolts(uint8_t input, uint16_t raw) {
  PicoCVLoadCalibrationOnce();
  const float zero = input == 0 ? picoCVCalibration.cv1_zero_counts : picoCVCalibration.cv2_zero_counts;
  const float counts_per_volt =
      input == 0 ? picoCVCalibration.cv1_counts_per_volt : picoCVCalibration.cv2_counts_per_volt;
  if (counts_per_volt <= 1.0f) {
    return 0.0f;
  }

  float volts = (zero - (float)raw) / counts_per_volt;
  const float abs_volts = volts < 0.0f ? -volts : volts;
  if (abs_volts < PICOPRO_CV_ZERO_DEADBAND_VOLTS) {
    return 0.0f;
  }
  return volts;
}

static inline float PicoCVCountsToVolts(uint8_t input, float counts) {
  PicoCVLoadCalibrationOnce();
  const float counts_per_volt =
      input == 0 ? picoCVCalibration.cv1_counts_per_volt
                 : picoCVCalibration.cv2_counts_per_volt;
  if (counts_per_volt <= 1.0f) {
    return counts / PICO_BOOT_DEFAULT_CV1_COUNTS_PER_VOLT;
  }
  return counts / counts_per_volt;
}

static inline float PicoCVHysteresisVolts(uint8_t input) {
  return PicoCVCountsToVolts(input, PICOPRO_CV_HYSTERESIS_COUNTS);
}

static inline float PicoCVInputVolts(uint8_t input, float max_volts) {
  input = input == 0 ? 0 : 1;
  if (!PicoCVInputAnalogAvailable(input)) return 0.0f;
  const uint32_t now = millis();
  if (picoCVStableReady[input] && picoCVStableMs[input] == now) {
    const float cached = picoCVStableVolts[input];
    if (cached > max_volts) return max_volts;
    return cached < -max_volts ? -max_volts : cached;
  }

  const float raw = (float)PicoCVReadRaw(input);
  if (!picoCVStableReady[input]) {
    picoCVFilteredRaw[input] = raw;
    picoCVStableReady[input] = true;
  } else {
    const float diff = raw - picoCVFilteredRaw[input];
    const float abs_diff = diff < 0.0f ? -diff : diff;
    if (abs_diff > PICOPRO_CV_FILTER_SNAP_COUNTS) {
      // Large keyboard/sequencer steps must not turn into portamento.
      picoCVFilteredRaw[input] = raw;
    } else if (abs_diff > PICOPRO_CV_HYSTERESIS_COUNTS) {
      const uint32_t elapsed_ms = now - picoCVStableMs[input];
      const float dt = elapsed_ms == 0u ? 1.0f : (float)elapsed_ms;
      const float alpha = dt / (PICOPRO_CV_FILTER_TIME_MS + dt);
      picoCVFilteredRaw[input] += diff * alpha;
    }
  }

  picoCVStableVolts[input] =
      PicoCVRawToVolts(input, (uint16_t)(picoCVFilteredRaw[input] + 0.5f));
  picoCVStableMs[input] = now;
  if (picoCVStableVolts[input] > max_volts) return max_volts;
  return picoCVStableVolts[input] < -max_volts
             ? -max_volts
             : picoCVStableVolts[input];
}

static inline float PicoCVApplyVoctDeadband(uint8_t input, float delta) {
  const float threshold = PicoCVHysteresisVolts(input == 0 ? 0 : 1);
  return delta > -threshold && delta < threshold ? 0.0f : delta;
}

static inline float PicoCVVoltsToNormalized(float volts) {
  if (volts < 0.0f) {
    const float normalized = volts / PICOPRO_CV_NEGATIVE_FULL_SCALE_VOLTS;
    return normalized < -1.0f ? -1.0f : normalized;
  }
  const float normalized = volts / PICOPRO_CV_POSITIVE_FULL_SCALE_VOLTS;
  return normalized > 1.0f ? 1.0f : normalized;
}

static inline float PicoCVNormalizedInput(uint8_t input) {
  input = input == 0 ? 0 : 1;
  if (!PicoCVInputAnalogAvailable(input)) return 0.0f;
  return PicoCVVoltsToNormalized(PicoCVInputVolts(input, 8.0f));
}

static inline float PicoCVLfoRandomBipolar(PicoCVAssignment *assignment) {
  assignment->lfo_random_state = assignment->lfo_random_state * 1664525u +
                                 1013904223u;
  return ((assignment->lfo_random_state >> 8) *
          (1.0f / 8388607.5f)) - 1.0f;
}

static inline float PicoCVNormalizedLfo(uint8_t index) {
  index = PicoCVClampIndex(index);
  PicoCVAssignment *assignment = &picoCVAssignments[index];
  const uint32_t now = micros();
  if (!assignment->lfo_ready) {
    assignment->lfo_phase = 0.0f;
    assignment->lfo_random_from = PicoCVLfoRandomBipolar(assignment);
    assignment->lfo_random_to = PicoCVLfoRandomBipolar(assignment);
    assignment->lfo_random_step = PicoCVLfoRandomBipolar(assignment);
    assignment->lfo_last_us = now;
    assignment->lfo_ready = true;
  } else {
    const uint32_t elapsed_us = now - assignment->lfo_last_us;
    assignment->lfo_last_us = now;
    const float advanced_phase =
        assignment->lfo_phase +
        (float)elapsed_us * 0.000001f * assignment->lfo_frequency_hz;
    assignment->lfo_phase = advanced_phase - floorf(advanced_phase);
    if (advanced_phase >= 1.0f) {
      assignment->lfo_random_from = assignment->lfo_random_to;
      assignment->lfo_random_to = PicoCVLfoRandomBipolar(assignment);
      assignment->lfo_random_step = PicoCVLfoRandomBipolar(assignment);
    }
  }

  const float phase = assignment->lfo_phase;
  switch (assignment->lfo_wave) {
    case 1: return phase * 2.0f - 1.0f;  // saw
    case 2: return 1.0f - 4.0f * fabsf(phase - 0.5f);  // triangle
    case 3: return phase < 0.5f ? 1.0f : -1.0f;  // square
    case 4: return assignment->lfo_random_from +
                   (assignment->lfo_random_to - assignment->lfo_random_from) *
                   phase;  // smooth random
    case 5: return assignment->lfo_random_step;  // stepped random
    default: return sinf(phase * 6.28318530717958647692f);
  }
}

static inline float PicoCVNormalizedModulation(uint8_t index) {
  index = PicoCVClampIndex(index);
  return picoCVAssignments[index].input == PICOPRO_CV_INPUT_LFO
             ? PicoCVNormalizedLfo(index)
             : PicoCVNormalizedInput(picoCVAssignments[index].input);
}

static inline void PicoCVCommitAssignment(uint8_t index, uint8_t input, int8_t amount) {
  index = PicoCVClampIndex(index);
  input = min(input, (uint8_t)PICOPRO_CV_INPUT_LFO);
  if (input == PICOPRO_CV_INPUT_LFO && amount == PICOPRO_CV_VOCT_AMOUNT) {
    amount = 10;
  }
  if (amount == PICOPRO_CV_VOCT_AMOUNT && !PicoCVIndexIsFrequency(index)) {
    amount = 0;
  }

  PicoCVAssignment *assignment = &picoCVAssignments[index];
  assignment->input = input;
  assignment->amount = amount;
  assignment->baseline_volts = 0.0f;
  assignment->baseline_ready = false;
  if (input == PICOPRO_CV_INPUT_LFO) {
    assignment->lfo_frequency_hz =
        PicoCVLfoFrequencyHz(assignment->lfo_freq_code);
    assignment->lfo_ready = false;
  }
  picoCVDisplayReady[index] = false;
  picoCVDisplayDirty[index] = true;
}

static inline int16_t PicoCVVoctValue(uint8_t index,
                                      uint8_t input,
                                      int16_t base,
                                      int16_t min_value,
                                      int16_t max_value) {
  const float volts = PicoCVInputVolts(input, 8.0f);
  const float delta_volts = PicoCVApplyVoctDeadband(input, volts);
  if (delta_volts == 0.0f) {
    return base;
  }

  const float modulated = (float)base * powf(2.0f, delta_volts);
  return PicoCVClampValue((int32_t)(modulated + 0.5f), min_value, max_value);
}

// V/oct oscillators must not quantize to whole hertz before calculating their
// phase increment. At 110 Hz, a one-hertz step is already about 15.7 cents and
// makes harmless ADC movement audible as pitch wobble.
static inline float PicoCVModulatedFrequencyHz(uint8_t index,
                                               float base_hz,
                                               float min_hz,
                                               float max_hz) {
  index = PicoCVClampIndex(index);
  const int8_t amount = picoCVAssignments[index].amount;
  float frequency = base_hz;

  if (amount != 0 &&
      (amount != PICOPRO_CV_VOCT_AMOUNT || !PicoCVIndexIsFrequency(index))) {
    const float drive = PicoCVNormalizedModulation(index) *
                        (float)amount / 10.0f;
    if (drive > 0.0f && base_hz > 0.0f && max_hz > base_hz) {
      frequency = base_hz * powf(max_hz / base_hz, drive);
    } else if (drive < 0.0f && min_hz > 0.0f && base_hz > min_hz) {
      frequency = base_hz * powf(base_hz / min_hz, drive);
    }
  } else if (amount == PICOPRO_CV_VOCT_AMOUNT) {
    const uint8_t input = picoCVAssignments[index].input;
    const float volts = PicoCVInputVolts(input, 8.0f);
    const float delta_volts = PicoCVApplyVoctDeadband(input, volts);
    if (delta_volts != 0.0f) {
      frequency = base_hz * powf(2.0f, delta_volts);
    }
  }

  if (frequency < min_hz) frequency = min_hz;
  if (frequency > max_hz) frequency = max_hz;
  PicoCVStoreDisplayValue(index,
                          (int16_t)(frequency + 0.5f),
                          amount != 0);
  return frequency;
}

static inline int16_t PicoCVModulatedValue(uint8_t index, int16_t base, int16_t min_value, int16_t max_value) {
  index = PicoCVClampIndex(index);
  const int8_t amount = picoCVAssignments[index].amount;
  if (amount == 0 || max_value <= min_value) {
    PicoCVStoreDisplayValue(index, base, false);
    return base;
  }

  if (amount == PICOPRO_CV_VOCT_AMOUNT && PicoCVIndexIsFrequency(index)) {
    const int16_t value = PicoCVVoctValue(index, picoCVAssignments[index].input, base, min_value, max_value);
    PicoCVStoreDisplayValue(index, value, true);
    return value;
  }

  const float normalized = PicoCVNormalizedModulation(index);
  const float drive = normalized * (float)amount / 10.0f;
  const float available = drive >= 0.0f
                              ? (float)((int32_t)max_value - base)
                              : (float)((int32_t)base - min_value);
  const float scaled_delta = drive * available;
  const int32_t delta = (int32_t)(scaled_delta >= 0.0f ? scaled_delta + 0.5f : scaled_delta - 0.5f);
  const int16_t value = PicoCVClampValue((int32_t)base + delta, min_value, max_value);
  PicoCVStoreDisplayValue(index, value, true);
  return value;
}

static inline void PicoCVAmountText(int8_t amount, char *out, size_t len) {
  if (len == 0) return;
  if (amount == PICOPRO_CV_VOCT_AMOUNT) {
    snprintf(out, len, "v/oct");
    return;
  }
  const uint8_t abs_amount = amount < 0 ? (uint8_t)(-amount) : (uint8_t)amount;
  snprintf(out, len, "%c%u.%u", amount < 0 ? '-' : '+',
           abs_amount / 10u, abs_amount % 10u);
}

static inline int16_t PicoCVCenteredX(int16_t x, int16_t w, uint8_t len) {
  return x + ((w - (int16_t)len * 6) >> 1);
}

static inline void PicoCVDrawCenteredText(const char *text, int16_t y) {
  const uint8_t len = strlen(text);
  display.setCursor(PicoCVCenteredX(6, 52, len), y);
  display.write((const uint8_t *)text, len);
}

static inline void PicoCVDrawHeaderText(const char *text) {
  display.fillRect(7, 2, 50, 9, WHITE);
  display.setTextColor(BLACK, WHITE);
  PicoCVDrawCenteredText(text, 3);
  display.setTextColor(WHITE, BLACK);
}

static inline void PicoCVDrawSourceOption(int16_t x, char label, bool selected) {
  if (selected) {
    display.fillRect(x, 16, 12, 10, WHITE);
    display.setTextColor(BLACK, WHITE);
  } else {
    display.drawRect(x, 16, 12, 10, WHITE);
    display.setTextColor(WHITE, BLACK);
  }
  display.setCursor(x + 3, 17);
  display.write((uint8_t)label);
  display.setTextColor(WHITE, BLACK);
}

static inline void PicoCVLfoFrequencyText(uint8_t code, char *out, size_t len) {
  const uint16_t hz_x100 = (uint16_t)(PicoCVLfoFrequencyHz(code) * 100.0f + 0.5f);
  snprintf(out, len, "%u.%02uhz", hz_x100 / 100u, hz_x100 % 100u);
}

static inline void PicoCVDrawOverlay(const menu *item) {
  (void)item;
  const char *source_name = picoCVSelectedInput == 0
                                ? "cv1"
                                : (picoCVSelectedInput == 1 ? "cv2" : "lfo");
  const char *parameter_row;
  const char *value_row;
  char value_text[12];

  if (picoCVUiState == PICOPRO_CV_UI_SELECT_INPUT) {
    parameter_row = "src";
    value_row = source_name;
  } else if (picoCVUiState == PICOPRO_CV_UI_LFO_WAVE) {
    parameter_row = "wave";
    value_row = picoCVLfoWaveNames[picoCVEditLfoWave];
  } else if (picoCVUiState == PICOPRO_CV_UI_LFO_FREQ) {
    parameter_row = "freq";
    PicoCVLfoFrequencyText(picoCVEditLfoFreqCode,
                           value_text, sizeof(value_text));
    value_row = value_text;
  } else {
    parameter_row = "amt";
    PicoCVAmountText(picoCVEditAmount, value_text, sizeof(value_text));
    value_row = value_text;
  }

  display.fillRect(6, 1, 52, 30, BLACK);
  display.drawRect(6, 1, 52, 30, WHITE);
  display.setTextColor(WHITE, BLACK);
  if (picoCVUiState == PICOPRO_CV_UI_SELECT_INPUT) {
    PicoCVDrawHeaderText("src");
    PicoCVDrawSourceOption(10, '1', picoCVSelectedInput == 0);
    PicoCVDrawSourceOption(26, '2', picoCVSelectedInput == 1);
    PicoCVDrawSourceOption(42, 'L',
                           picoCVSelectedInput == PICOPRO_CV_INPUT_LFO);
  } else {
    PicoCVDrawHeaderText(source_name);
    PicoCVDrawCenteredText(parameter_row, 12);
    PicoCVDrawCenteredText(value_row, 21);
  }
  display.setTextColor(WHITE, BLACK);
  display.display();
  displaytimer = millis();
}

static inline uint8_t PicoCVServiceOverlay(const menu *menus,
                                           uint8_t count,
                                           int16_t enc,
                                           bool button_down,
                                           ClickEncoder::Button button) {
  PicoCVBindMenus(menus, count);
  if (picoCVUiState == PICOPRO_CV_UI_OFF) {
    return 0;
  }

  if (button == ClickEncoder::DoubleClicked) {
    PicoCVCommitAssignment(picoCVEditIndex, 0, 0);
    picoCVUiState = PICOPRO_CV_UI_OFF;
    picoCVButtonWasDown = false;
    return 2;
  }

  bool dirty = false;
  const bool confirm_click = button_down && !picoCVButtonWasDown;
  picoCVButtonWasDown = button_down;

  if (picoCVUiState == PICOPRO_CV_UI_SELECT_INPUT) {
    if (enc != 0) {
      const uint8_t next_input = constrain(
          (int16_t)picoCVSelectedInput + enc, 0,
          (int16_t)PICOPRO_CV_INPUT_LFO);
      if (picoCVSelectedInput != next_input) {
        picoCVSelectedInput = next_input;
        dirty = true;
      }
    }
    if (confirm_click) {
      picoCVEditAmount = picoCVAssignments[picoCVEditIndex].amount;
      if (picoCVEditAmount == PICOPRO_CV_VOCT_AMOUNT &&
          (picoCVSelectedInput == PICOPRO_CV_INPUT_LFO ||
           !PicoCVIndexIsFrequency(picoCVEditIndex))) {
        picoCVEditAmount = 0;
      }
      picoCVUiState = picoCVSelectedInput == PICOPRO_CV_INPUT_LFO
                          ? PICOPRO_CV_UI_LFO_WAVE
                          : PICOPRO_CV_UI_AMOUNT;
      dirty = true;
    }
  } else if (picoCVUiState == PICOPRO_CV_UI_LFO_WAVE) {
    if (enc != 0) {
      const uint8_t next = constrain(
          (int16_t)picoCVEditLfoWave + enc, 0,
          (int16_t)(PICOPRO_CV_LFO_WAVE_COUNT - 1u));
      if (next != picoCVEditLfoWave) {
        picoCVEditLfoWave = next;
        dirty = true;
      }
    }
    if (confirm_click) {
      picoCVUiState = PICOPRO_CV_UI_LFO_FREQ;
      dirty = true;
    }
  } else if (picoCVUiState == PICOPRO_CV_UI_LFO_FREQ) {
    if (enc != 0) {
      const uint8_t next = constrain(
          (int16_t)picoCVEditLfoFreqCode + enc, 0,
          (int16_t)(PICOPRO_CV_LFO_FREQ_STEPS - 1u));
      if (next != picoCVEditLfoFreqCode) {
        picoCVEditLfoFreqCode = next;
        dirty = true;
      }
    }
    if (confirm_click) {
      picoCVUiState = PICOPRO_CV_UI_AMOUNT;
      dirty = true;
    }
  } else if (picoCVUiState == PICOPRO_CV_UI_AMOUNT) {
    if (enc != 0) {
      int16_t next = picoCVEditAmount + enc;
      if (next < -10) next = -10;
      const int16_t max_amount = picoCVSelectedInput != PICOPRO_CV_INPUT_LFO &&
                                         PicoCVIndexIsFrequency(picoCVEditIndex)
                                     ? PICOPRO_CV_VOCT_AMOUNT
                                     : 10;
      if (next > max_amount) next = max_amount;
      if (picoCVEditAmount != next) {
        picoCVEditAmount = (int8_t)next;
        dirty = true;
      }
    }
    if (confirm_click) {
      PicoCVAssignment *assignment = &picoCVAssignments[picoCVEditIndex];
      assignment->lfo_wave = picoCVEditLfoWave;
      assignment->lfo_freq_code = picoCVEditLfoFreqCode;
      PicoCVCommitAssignment(picoCVEditIndex, picoCVSelectedInput, picoCVEditAmount);
      picoCVUiState = PICOPRO_CV_UI_WAIT_RELEASE_DONE;
      picoCVButtonWasDown = true;
      dirty = true;
    }
  } else if (picoCVUiState == PICOPRO_CV_UI_WAIT_RELEASE_DONE) {
    // Keep the overlay alive through the double-click window so a second
    // short press can still clear an assignment from the final amount page.
    if (button == ClickEncoder::Clicked) {
      picoCVUiState = PICOPRO_CV_UI_OFF;
      picoCVButtonWasDown = false;
      return 2;
    }
  }

  if (picoCVEditIndex >= count) {
    picoCVEditIndex = count > 0 ? count - 1 : 0;
    dirty = true;
  }
  if (dirty) {
    PicoCVDrawOverlay(&menus[picoCVEditIndex]);
  }
  return 1;
}

#endif // PICOPRO_CV_MODULATION_H_
