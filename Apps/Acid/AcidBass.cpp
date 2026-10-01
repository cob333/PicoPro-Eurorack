#include "AcidBass.h"

#include <math.h>

// The four-stage filter topology and coefficient fit below are adapted from
// Robin Schmidt's Open303 TeeBeeFilter, released under the MIT license.
// Copyright (c) 2009 Robin Schmidt (www.rs-met.com).

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kFeedbackHighpassHz = 150.0f;
constexpr int8_t kReferenceNote = 24;  // C3 in Acid's C1..B5 note numbering.

}  // namespace

void AcidBass::resetDefaults() {
  config_.frequency = 131u;
  config_.wave = ACID_WAVE_SAW;
  config_.cutoff = 1000u;
  config_.resonance = 350u;
  config_.env_mod = 600u;
  config_.decay_ms = 600u;
  config_.accent = 500u;
  config_.slide_ms = 60u;
  config_.drive = 120u;
  config_.level = 800u;
}

void AcidBass::sanitize() {
  config_.frequency = constrain((int)config_.frequency, 20, 2000);
  config_.wave = min(config_.wave, (uint8_t)ACID_WAVE_TRIANGLE);
  config_.cutoff = constrain((int)config_.cutoff, 20, 10000);
  config_.resonance = constrain((int)config_.resonance, 0, 1000);
  config_.env_mod = constrain((int)config_.env_mod, 0, 1000);
  config_.decay_ms = constrain((int)config_.decay_ms, 10, 3000);
  config_.accent = constrain((int)config_.accent, 0, 1000);
  config_.slide_ms = constrain((int)config_.slide_ms, 1, 200);
  config_.drive = constrain((int)config_.drive, 0, 1000);
  config_.level = constrain((int)config_.level, 0, 1000);
}

void AcidBass::begin(float sample_rate) {
  sample_rate_ = max(8000.0f, sample_rate);
  oversample_rate_ = sample_rate_ * kOversampling;
  inverse_oversample_rate_ = 1.0f / oversample_rate_;
  exit_gain_step_ = 1.0f / (0.020f * sample_rate_);
  resetDefaults();
  sanitize();
  publishConfig();
  feedback_lowpass_coefficient_ =
      1.0f - expf(-2.0f * kPi * kFeedbackHighpassHz / oversample_rate_);
  amp_attack_coefficient_ = expf(-1.0f / (0.0015f * sample_rate_));
  amp_release_coefficient_ = expf(-1.0f / (0.010f * sample_rate_));
  AcidBassControls controls;
  // Core1 is not running yet. Reuse the published snapshot instead of a
  // second copy of the config-to-controls conversion.
  if (readControls(&controls, &audio_config_revision_)) applyControls(controls);
  updateFilter(cutoff_);
}

void AcidBass::restoreConfig(const AcidBassConfig &config) {
  config_ = config;
  sanitize();
  publishConfig();
}

void AcidBass::publishConfig() {
  setModulatedControls((float)config_.frequency, config_);
}

void AcidBass::setModulatedControls(float frequency_hz,
                                    const AcidBassConfig &controls) {
  frequency_hz = constrain(frequency_hz, 20.0f, 2000.0f);
  // Only core0 writes these controls. An unchanged publication would force
  // core1 to recalculate all envelope/filter coefficients unnecessarily.
  if (config_revision_ != 0u &&
      shared_controls_.frequency_hz == frequency_hz &&
      shared_controls_.cutoff == controls.cutoff &&
      shared_controls_.resonance == controls.resonance &&
      shared_controls_.env_mod == controls.env_mod &&
      shared_controls_.decay_ms == controls.decay_ms &&
      shared_controls_.accent == controls.accent &&
      shared_controls_.slide_ms == controls.slide_ms &&
      shared_controls_.drive == controls.drive &&
      shared_controls_.level == controls.level &&
      shared_controls_.wave == controls.wave) return;
  __atomic_add_fetch(&config_revision_, 1u, __ATOMIC_ACQ_REL);
  shared_controls_.frequency_hz = frequency_hz;
  shared_controls_.cutoff = controls.cutoff;
  shared_controls_.resonance = controls.resonance;
  shared_controls_.env_mod = controls.env_mod;
  shared_controls_.decay_ms = controls.decay_ms;
  shared_controls_.accent = controls.accent;
  shared_controls_.slide_ms = controls.slide_ms;
  shared_controls_.drive = controls.drive;
  shared_controls_.level = controls.level;
  shared_controls_.wave = controls.wave;
  __atomic_add_fetch(&config_revision_, 1u, __ATOMIC_RELEASE);
}

bool AcidBass::readControls(AcidBassControls *controls,
                            uint32_t *revision) const {
  if (controls == nullptr || revision == nullptr) return false;
  for (uint8_t attempt = 0; attempt < 4u; ++attempt) {
    const uint32_t before = __atomic_load_n(&config_revision_, __ATOMIC_ACQUIRE);
    if (before == *revision) return false;
    if (before & 1u) continue;
    AcidBassControls candidate;
    candidate.frequency_hz = shared_controls_.frequency_hz;
    candidate.cutoff = shared_controls_.cutoff;
    candidate.resonance = shared_controls_.resonance;
    candidate.env_mod = shared_controls_.env_mod;
    candidate.decay_ms = shared_controls_.decay_ms;
    candidate.accent = shared_controls_.accent;
    candidate.slide_ms = shared_controls_.slide_ms;
    candidate.drive = shared_controls_.drive;
    candidate.level = shared_controls_.level;
    candidate.wave = shared_controls_.wave;
    const uint32_t after = __atomic_load_n(&config_revision_, __ATOMIC_ACQUIRE);
    if (before == after && !(after & 1u)) {
      *controls = candidate;
      *revision = after;
      return true;
    }
  }
  return false;
}

void AcidBass::setFrequency(int32_t value) {
  config_.frequency = constrain(value, 20L, 2000L);
  publishConfig();
}

void AcidBass::setWave(int16_t value) {
  config_.wave = constrain((int)value, (int)ACID_WAVE_SAW,
                           (int)ACID_WAVE_TRIANGLE);
  publishConfig();
}

void AcidBass::setCutoff(int32_t value) {
  config_.cutoff = constrain(value, 20L, 10000L);
  publishConfig();
}

void AcidBass::setResonance(int16_t value) {
  config_.resonance = constrain((int)value, 0, 1000);
  publishConfig();
}

void AcidBass::setEnvMod(int16_t value) {
  config_.env_mod = constrain((int)value, 0, 1000);
  publishConfig();
}

void AcidBass::setDecay(int32_t value) {
  config_.decay_ms = constrain(value, 10L, 3000L);
  publishConfig();
}

void AcidBass::setAccent(int16_t value) {
  config_.accent = constrain((int)value, 0, 1000);
  publishConfig();
}

void AcidBass::setSlide(int16_t value) {
  config_.slide_ms = constrain((int)value, 1, 200);
  publishConfig();
}

void AcidBass::setDrive(int16_t value) {
  config_.drive = constrain((int)value, 0, 1000);
  publishConfig();
}

void AcidBass::setLevel(int16_t value) {
  config_.level = constrain((int)value, 0, 1000);
  publishConfig();
}

void AcidBass::applyControls(const AcidBassControls &controls) {
  wave_ = controls.wave;
  base_frequency_ = controls.frequency_hz;
  cutoff_ = controls.cutoff;
  const float resonance_raw = controls.resonance * 0.001f;
  resonance_ = (1.0f - expf(-3.0f * resonance_raw)) /
               (1.0f - expf(-3.0f));
  env_depth_octaves_ = 5.0f * controls.env_mod * 0.001f;
  env_decay_coefficient_ =
      expf(-1.0f / (0.001f * controls.decay_ms * oversample_rate_));
  accent_amount_ = controls.accent * 0.001f;
  const float slide_tau = max(0.0002f, controls.slide_ms * 0.0002f);
  slide_coefficient_ = expf(-1.0f / (slide_tau * oversample_rate_));
  drive_amount_ = controls.drive * 0.001f;
  drive_gain_ = 1.0f + 15.0f * drive_amount_;
  constexpr float kDriveReference = 0.25f;
  drive_makeup_ = kDriveReference /
                  softClip(kDriveReference * drive_gain_);
  level_ = controls.level * 0.001f;
  if (current_note_ != AcidSequencer::kMutedNote)
    setTargetNote(current_note_, false);
}

void AcidBass::setTargetNote(int8_t note, bool immediate) {
  current_note_ = note;
  const float semitones = (float)(note - kReferenceNote) / 12.0f;
  target_frequency_ = constrain(base_frequency_ * exp2f(semitones),
                                10.0f, 10000.0f);
  if (immediate) current_frequency_ = target_frequency_;
}

void AcidBass::handleEvent(const AcidSequenceEvent &event) {
  if (!event.playing || event.note == AcidSequencer::kMutedNote) {
    gate_ = false;
    gate_samples_remaining_ = 0;
    current_note_ = AcidSequencer::kMutedNote;
    return;
  }

  const bool can_slide = event.legato && voice_active_;
  setTargetNote(event.note, !can_slide);
  note_accent_ = (event.flags & ACID_STEP_ACCENT) ? accent_amount_ : 0.0f;
  gate_ = true;
  const float gate_fraction = (event.flags & ACID_STEP_SLIDE) ? 1.05f : 0.55f;
  gate_samples_remaining_ = max(
      1u, (uint32_t)(event.step_period_us * (sample_rate_ / 1000000.0f) *
                     gate_fraction));

  if (!can_slide) {
    env_value_ = 1.0f;
    if (!voice_active_) {
      phase_ = 0.0f;
      filter_y1_ = filter_y2_ = filter_y3_ = filter_y4_ = 0.0f;
      feedback_lowpass_ = 0.0f;
    }
  }
  voice_active_ = true;
}

float AcidBass::polyBlep(float phase, float phase_step) {
  if (phase < phase_step) {
    const float x = phase / phase_step;
    return x + x - x * x - 1.0f;
  }
  if (phase > 1.0f - phase_step) {
    const float x = (phase - 1.0f) / phase_step;
    return x * x + x + x + 1.0f;
  }
  return 0.0f;
}

float AcidBass::oscillatorSample(float phase, float phase_step) const {
  if (wave_ == ACID_WAVE_TRIANGLE)
    return 1.0f - 4.0f * fabsf(phase - 0.5f);
  if (wave_ == ACID_WAVE_SQUARE) {
    float value = phase < 0.5f ? 1.0f : -1.0f;
    value += polyBlep(phase, phase_step);
    float shifted = phase + 0.5f;
    if (shifted >= 1.0f) shifted -= 1.0f;
    value -= polyBlep(shifted, phase_step);
    return value;
  }
  return 2.0f * phase - 1.0f - polyBlep(phase, phase_step);
}

void AcidBass::updateFilter(float cutoff_hz) {
  cutoff_hz = constrain(cutoff_hz, 20.0f, 10000.0f);
  const float fx = cutoff_hz * (0.70710678118f * inverse_oversample_rate_);
  const float fx2 = fx * fx;
  filter_b0_ = (0.00045522346f + 6.1922189f * fx) /
               (1.0f + 12.358354f * fx + 4.4156345f * fx2);

  float scale = fx + 7198.6997f;
  scale = fx * scale - 5837.7917f;
  scale = fx * scale - 476.47308f;
  scale = fx * scale + 614.95611f;
  scale = fx * scale + 213.87126f;
  scale = fx * scale + 16.998792f;
  filter_k_ = scale * resonance_;
  filter_g_ = (((scale / 17.0f) - 1.0f) * resonance_ + 1.0f) *
              (1.0f + resonance_);
}

float AcidBass::filterSample(float input) {
  const float feedback = filter_k_ * filter_y4_;
  feedback_lowpass_ +=
      feedback_lowpass_coefficient_ * (feedback - feedback_lowpass_);
  const float highpass_feedback = feedback - feedback_lowpass_;
  const float y0 = input - highpass_feedback;
  filter_y1_ += 2.0f * filter_b0_ *
                (y0 - filter_y1_ + filter_y2_);
  filter_y2_ += filter_b0_ *
                (filter_y1_ - 2.0f * filter_y2_ + filter_y3_);
  filter_y3_ += filter_b0_ *
                (filter_y2_ - 2.0f * filter_y3_ + filter_y4_);
  filter_y4_ += filter_b0_ * (filter_y3_ - 2.0f * filter_y4_);
  return 2.0f * filter_g_ * filter_y4_;
}

float AcidBass::softClip(float value) {
  if (value >= 1.0f) return 0.6666667f;
  if (value <= -1.0f) return -0.6666667f;
  return value - value * value * value * 0.3333333f;
}

float AcidBass::renderOversample() {
  current_frequency_ = target_frequency_ +
      slide_coefficient_ * (current_frequency_ - target_frequency_);
  const float phase_step = min(0.45f, current_frequency_ * inverse_oversample_rate_);
  const float oscillator = oscillatorSample(phase_, phase_step);
  phase_ += phase_step;
  if (phase_ >= 1.0f) phase_ -= 1.0f;

  env_value_ *= env_decay_coefficient_;
  if (++filter_divider_ >= 16u) {
    filter_divider_ = 0;
    const float envelope = min(1.5f, env_value_ * (1.0f + note_accent_));
    updateFilter(cutoff_ * exp2f(env_depth_octaves_ * envelope));
  }
  return filterSample(oscillator * 0.55f);
}

float AcidBass::process(AcidSequencer &sequencer) {
  sequencer.serviceAudio();
  if (++control_divider_ >= 64u) {
    control_divider_ = 0;
    AcidBassControls next_controls;
    uint32_t next_revision = audio_config_revision_;
    if (readControls(&next_controls, &next_revision)) {
      audio_config_revision_ = next_revision;
      applyControls(next_controls);
    }
  }

  AcidSequenceEvent event;
  uint32_t next_sequence_revision = sequence_revision_;
  if (sequencer.readEvent(&event, &next_sequence_revision)) {
    sequence_revision_ = next_sequence_revision;
    handleEvent(event);
  }

  if (gate_samples_remaining_ > 0u && --gate_samples_remaining_ == 0u)
    gate_ = false;
  const float amp_coefficient = gate_ ? amp_attack_coefficient_
                                      : amp_release_coefficient_;
  amp_value_ = (gate_ ? 1.0f : 0.0f) +
               amp_coefficient * (amp_value_ - (gate_ ? 1.0f : 0.0f));
  if (!gate_ && amp_value_ < 0.0001f) {
    amp_value_ = 0.0f;
    voice_active_ = false;
  }

  float output = 0.0f;
  if (voice_active_) {
    for (uint8_t i = 0; i < kOversampling; ++i) output += renderOversample();
    output *= 1.0f / kOversampling;
    // The wet shaper is level-compensated at a representative signal level
    // and crossfaded with the clean filter output. Drive=0 is a true bypass;
    // increasing Drive primarily changes compression and harmonics.
    const float driven = softClip(output * drive_gain_) * drive_makeup_;
    output = (output + drive_amount_ * (driven - output)) * 1.5f;
    output *= amp_value_ * (1.0f + 0.6f * note_accent_) * level_ * 0.55f;
  }

  if (exiting_) {
    exit_gain_ = max(0.0f, exit_gain_ - exit_gain_step_);
  }
  return constrain(output * exit_gain_, -1.0f, 1.0f);
}

void AcidBass::prepareExit() {
  exiting_ = true;
  delay(24);
}
