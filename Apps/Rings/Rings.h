// Mutable Instruments Rings DSP host adapted from the Pico-Eurorack Rings app.
#ifndef PICOPRO_RINGS_ENGINE_H_
#define PICOPRO_RINGS_ENGINE_H_

#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <STMLIB.h>
#include <RINGS_ARDUINO.h>

namespace rings {
float Dsp::sr = static_cast<float>(SAMPLERATE);
float Dsp::a3 = 440.0f / static_cast<float>(SAMPLERATE);
}

namespace pico_rings {

constexpr float kSampleRate = static_cast<float>(SAMPLERATE);
constexpr size_t kBlockSize = rings::kMaxBlockSize;
constexpr uint8_t kNumModels = rings::RESONATOR_MODEL_LAST;
constexpr float kMinNote = 12.0f;
constexpr float kMaxNote = 108.0f;
constexpr float kOutputGateThreshold = 0.0012f;
constexpr uint8_t kOutputGateCloseBlocks = 32;

struct EngineState {
  rings::Part part;
  rings::Strummer strummer;
  rings::PerformanceState performance;
  rings::Patch patch;
  uint16_t reverb_buffer[32768];
  float silence[kBlockSize];
  float out[kBlockSize];
  float aux[kBlockSize];
  int16_t render_buffer[kBlockSize];
  uint8_t active_model;
  uint8_t active_polyphony;
  uint8_t quiet_blocks;
  bool output_gate_open;
};

static volatile float g_structure = 0.25f;
static volatile float g_damping = 0.55f;
static volatile float g_brightness = 0.55f;
static volatile float g_note = 48.0f;
static volatile float g_position = 0.25f;
static volatile uint8_t g_polyphony = 1;
static volatile uint8_t g_model = rings::RESONATOR_MODEL_MODAL;
static volatile bool g_trigger_pending = false;
static volatile bool g_muted = false;
static EngineState g_engine;

inline uint8_t ClampPolyphony(uint8_t requested, uint8_t model) {
  uint8_t limited = constrain(requested, 1, rings::kMaxPolyphony);
  if (model == rings::RESONATOR_MODEL_FM_VOICE && limited > 2) limited = 2;
  return limited;
}

inline float OutputGainForModel(uint8_t model) {
  if (model == rings::RESONATOR_MODEL_FM_VOICE) return 0.45f;
  if (model == rings::RESONATOR_MODEL_STRING_AND_REVERB) return 0.5f;
  return 0.85f;
}

inline void Init() {
  memset(&g_engine, 0, sizeof(g_engine));
  rings::Dsp::setSr(kSampleRate);
  g_engine.part.Init(g_engine.reverb_buffer);
  g_engine.strummer.Init(0.01f, kSampleRate / static_cast<float>(kBlockSize));
  g_engine.part.set_polyphony(1);
  g_engine.part.set_model(rings::RESONATOR_MODEL_MODAL);
  g_engine.part.set_bypass(false);
  g_engine.performance.internal_exciter = true;
  g_engine.performance.internal_strum = false;
  g_engine.performance.internal_note = false;
  g_engine.performance.tonic = 12.0f;
  g_engine.performance.note = 48.0f;
  g_engine.patch.structure = 0.25f;
  g_engine.patch.damping = 0.55f;
  g_engine.patch.brightness = 0.55f;
  g_engine.patch.position = 0.25f;
  g_engine.active_model = rings::RESONATOR_MODEL_MODAL;
  g_engine.active_polyphony = 1;
  g_engine.quiet_blocks = kOutputGateCloseBlocks;
}

inline void SetUiState(float structure, float damping, float brightness,
                       float note, float position, uint8_t polyphony,
                       uint8_t model) {
  g_structure = constrain(structure, 0.0f, 1.0f);
  g_damping = constrain(damping, 0.0f, 1.0f);
  g_brightness = constrain(brightness, 0.0f, 1.0f);
  g_note = constrain(note, kMinNote, kMaxNote);
  g_position = constrain(position, 0.0f, 1.0f);
  g_polyphony = polyphony;
  g_model = constrain(model, 0, kNumModels - 1);
}

inline void QueueTrigger() { g_trigger_pending = true; }
inline void Mute(bool muted) { g_muted = muted; }

inline void RenderBlock() {
  const uint8_t model = constrain(g_model, 0, kNumModels - 1);
  const uint8_t polyphony = ClampPolyphony(g_polyphony, model);
  if (model != g_engine.active_model) {
    g_engine.part.set_model(static_cast<rings::ResonatorModel>(model));
    g_engine.active_model = model;
  }
  if (polyphony != g_engine.active_polyphony) {
    g_engine.part.set_polyphony(polyphony);
    g_engine.active_polyphony = polyphony;
  }

  g_engine.patch.structure = g_structure;
  g_engine.patch.damping = g_damping;
  g_engine.patch.brightness = g_brightness;
  g_engine.patch.position = g_position;
  g_engine.performance.tonic = 12.0f;
  g_engine.performance.note = g_note;
  g_engine.performance.fm = 0.0f;
  g_engine.performance.chord = static_cast<int32_t>(
      roundf(g_structure * static_cast<float>(rings::kNumChords - 1)));
  g_engine.performance.strum = g_trigger_pending;
  g_trigger_pending = false;

  g_engine.strummer.Process(g_engine.silence, kBlockSize, &g_engine.performance);
  const bool strummed = g_engine.performance.strum;
  g_engine.part.Process(g_engine.performance, g_engine.patch, g_engine.silence,
                        g_engine.out, g_engine.aux, kBlockSize);

  const float gain = OutputGainForModel(model) * 0.5f;
  float peak = 0.0f;
  for (size_t i = 0; i < kBlockSize; ++i) {
    const float sample = (g_engine.out[i] + g_engine.aux[i]) * gain;
    peak = max(peak, fabsf(sample));
    g_engine.render_buffer[i] = stmlib::Clip16(
        static_cast<int32_t>(sample * 32767.0f));
  }

  if (strummed || peak > kOutputGateThreshold) {
    g_engine.output_gate_open = true;
    g_engine.quiet_blocks = 0;
  } else if (g_engine.quiet_blocks < kOutputGateCloseBlocks) {
    ++g_engine.quiet_blocks;
  } else {
    g_engine.output_gate_open = false;
  }
  if (!g_engine.output_gate_open || g_muted) {
    memset(g_engine.render_buffer, 0, sizeof(g_engine.render_buffer));
  }
}

inline int16_t NextSample() {
  static size_t index = kBlockSize;
  if (index >= kBlockSize) {
    RenderBlock();
    index = 0;
  }
  return g_engine.render_buffer[index++];
}

}  // namespace pico_rings
#endif
