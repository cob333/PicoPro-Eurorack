#ifndef PICOPRO_ACID_BASS_H_
#define PICOPRO_ACID_BASS_H_

#include <Arduino.h>

#include "AcidSequencer.h"

enum AcidBassWave : uint8_t {
  ACID_WAVE_SAW = 0,
  ACID_WAVE_SQUARE = 1,
  ACID_WAVE_TRIANGLE = 2,
};

struct AcidBassConfig {
  uint16_t frequency;
  uint16_t cutoff;
  uint16_t resonance;
  uint16_t env_mod;
  uint16_t decay_ms;
  uint16_t accent;
  uint16_t slide_ms;
  uint16_t drive;
  uint16_t level;
  uint8_t wave;
};

struct AcidBassControls {
  float frequency_hz;
  uint16_t cutoff;
  uint16_t resonance;
  uint16_t env_mod;
  uint16_t decay_ms;
  uint16_t accent;
  uint16_t slide_ms;
  uint16_t drive;
  uint16_t level;
  uint8_t wave;
};

class AcidBass {
 public:
  void begin(float sample_rate);
  float process(AcidSequencer &sequencer);
  void prepareExit();

  const AcidBassConfig &config() const { return config_; }
  void restoreConfig(const AcidBassConfig &config);
  void setModulatedControls(float frequency_hz,
                            const AcidBassConfig &controls);

  void setFrequency(int32_t value);
  void setWave(int16_t value);
  void setCutoff(int32_t value);
  void setResonance(int16_t value);
  void setEnvMod(int16_t value);
  void setDecay(int32_t value);
  void setAccent(int16_t value);
  void setSlide(int16_t value);
  void setDrive(int16_t value);
  void setLevel(int16_t value);

 private:
  static constexpr uint8_t kOversampling = 2u;

  void resetDefaults();
  void sanitize();
  void publishConfig();
  bool readControls(AcidBassControls *controls, uint32_t *revision) const;
  void applyControls(const AcidBassControls &controls);
  void handleEvent(const AcidSequenceEvent &event);
  void setTargetNote(int8_t note, bool immediate);
  float renderOversample();
  float oscillatorSample(float phase, float phase_step) const;
  void updateFilter(float cutoff_hz);
  float filterSample(float input);
  static float polyBlep(float phase, float phase_step);
  static float softClip(float value);

  AcidBassConfig config_ = {};
  volatile AcidBassControls shared_controls_ = {};
  volatile uint32_t config_revision_ = 0;
  volatile bool exiting_ = false;

  float sample_rate_ = 44100.0f;
  float oversample_rate_ = 88200.0f;
  float inverse_oversample_rate_ = 1.0f / 88200.0f;
  float exit_gain_step_ = 1.0f / (0.020f * 44100.0f);
  uint32_t audio_config_revision_ = UINT32_MAX;
  uint32_t sequence_revision_ = UINT32_MAX;
  uint16_t control_divider_ = 0;
  uint8_t filter_divider_ = 0;

  uint8_t wave_ = ACID_WAVE_SAW;
  int8_t current_note_ = AcidSequencer::kMutedNote;
  float base_frequency_ = 131.0f;
  float current_frequency_ = 131.0f;
  float target_frequency_ = 131.0f;
  float slide_coefficient_ = 0.0f;
  float phase_ = 0.0f;

  float cutoff_ = 1000.0f;
  float resonance_ = 0.0f;
  float env_depth_octaves_ = 0.0f;
  float env_value_ = 0.0f;
  float env_decay_coefficient_ = 0.999f;
  float accent_amount_ = 0.0f;
  float note_accent_ = 0.0f;

  float amp_value_ = 0.0f;
  float amp_attack_coefficient_ = 0.98f;
  float amp_release_coefficient_ = 0.99f;
  bool gate_ = false;
  bool voice_active_ = false;
  uint32_t gate_samples_remaining_ = 0;

  float drive_gain_ = 1.0f;
  float drive_amount_ = 0.0f;
  float drive_makeup_ = 1.0f;
  float level_ = 0.8f;
  float exit_gain_ = 1.0f;

  float filter_b0_ = 0.01f;
  float filter_k_ = 0.0f;
  float filter_g_ = 1.0f;
  float filter_y1_ = 0.0f;
  float filter_y2_ = 0.0f;
  float filter_y3_ = 0.0f;
  float filter_y4_ = 0.0f;
  float feedback_lowpass_ = 0.0f;
  float feedback_lowpass_coefficient_ = 0.01f;
};

#endif
