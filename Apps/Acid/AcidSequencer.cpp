#include "AcidSequencer.h"

#include "CVInput.h"
#include "PicoAppState.h"

namespace {

constexpr uint32_t kStateTag = PICOPRO_APP_STATE_TAG('A', 'C', 'S', '1');
constexpr uint32_t kClockDebounceUs = 500u;
constexpr uint32_t kExternalTimeoutMinUs = 250000u;
constexpr uint32_t kExternalTimeoutMaxUs = 2000000u;

struct AcidPersistentState {
  uint8_t version;
  AcidSequenceConfig config;
  AcidStep steps[AcidSequencer::kMaxSteps];
};

static_assert(sizeof(AcidPersistentState) <= PICOPRO_APP_STATE_PAYLOAD_BYTES,
              "Acid sequence state exceeds PicoAppState payload");

uint32_t internalStepPeriodUs(uint16_t bpm) {
  return 15000000u / (uint32_t)constrain((int)bpm, 30, 300);
}

}  // namespace

void AcidSequencer::resetDefaults() {
  config_ = {120u, 0, ACID_CLOCK_INTERNAL, ACID_RATIO_X1, kMaxSteps};
  for (uint8_t i = 0; i < kMaxSteps; ++i) {
    steps_[i].note = kMutedNote;
    steps_[i].flags = 0;
  }
}

void AcidSequencer::sanitize() {
  config_.bpm = constrain((int)config_.bpm, 30, 300);
  config_.transpose = constrain((int)config_.transpose, -24, 24);
  config_.clock = min(config_.clock, (uint8_t)ACID_CLOCK_CV2);
  config_.ratio = min(config_.ratio, (uint8_t)ACID_RATIO_X4);
  config_.length = constrain((int)config_.length, 1, (int)kMaxSteps);
  for (uint8_t i = 0; i < kMaxSteps; ++i) {
    steps_[i].note = constrain((int)steps_[i].note,
                               (int)kMutedNote, (int)kMaxNote);
    steps_[i].flags &= (ACID_STEP_ACCENT | ACID_STEP_SLIDE);
  }
}

void AcidSequencer::begin() {
  resetDefaults();
  AcidPersistentState state;
  if (PicoAppStateLoad(kStateTag, &state, sizeof(state)) && state.version == 1u) {
    config_ = state.config;
    memcpy(steps_, state.steps, sizeof(steps_));
    sanitize();
  }
  stopAndReset();
  updateDigitalRole();
}

void AcidSequencer::copyState(AcidSequenceConfig *config, AcidStep *steps) const {
  if (config != nullptr) *config = config_;
  if (steps != nullptr) memcpy(steps, steps_, sizeof(steps_));
}

void AcidSequencer::restoreState(const AcidSequenceConfig &config,
                                 const AcidStep *steps) {
  config_ = config;
  if (steps != nullptr) memcpy(steps_, steps, sizeof(steps_));
  sanitize();
  stopAndReset();
  updateDigitalRole();
}

bool AcidSequencer::readEvent(AcidSequenceEvent *event,
                              uint32_t *revision) const {
  if (event == nullptr || revision == nullptr) return false;
  for (uint8_t attempt = 0; attempt < 4u; ++attempt) {
    const uint32_t before = __atomic_load_n(&event_revision_, __ATOMIC_ACQUIRE);
    if (before == *revision) return false;
    if (before & 1u) continue;
    AcidSequenceEvent candidate;
    candidate.note = event_.note;
    candidate.flags = event_.flags;
    candidate.legato = event_.legato;
    candidate.playing = event_.playing;
    candidate.step_period_us = event_.step_period_us;
    const uint32_t after = __atomic_load_n(&event_revision_, __ATOMIC_ACQUIRE);
    if (before == after && !(after & 1u)) {
      *event = candidate;
      *revision = after;
      return true;
    }
  }
  return false;
}

void AcidSequencer::updateDigitalRole() {
  PicoCVInputSelectDigitalRole((int8_t)config_.clock,
                               &digital_clock_input_, false);
  raw_clock_ = false;
  stable_clock_ = false;
  raw_changed_us_ = micros();
}

void AcidSequencer::setClock(int16_t value) {
  const uint8_t next = constrain((int)value, (int)ACID_CLOCK_INTERNAL,
                                 (int)ACID_CLOCK_CV2);
  if (config_.clock == next) return;
  config_.clock = next;
  stopAndReset();
  updateDigitalRole();
}

void AcidSequencer::setBpm(int16_t value) {
  config_.bpm = constrain((int)value, 30, 300);
  if (playing_ && !externalClock()) {
    next_step_us_ = micros() + internalStepPeriodUs(config_.bpm);
  }
}

void AcidSequencer::setRatio(int16_t value) {
  config_.ratio = constrain((int)value, (int)ACID_RATIO_DIV2,
                            (int)ACID_RATIO_X4);
  division_count_ = 0;
  subdivision_index_ = 0;
}

void AcidSequencer::setLength(int16_t value) {
  config_.length = constrain((int)value, 1, (int)kMaxSteps);
  if (active_step_ >= (int8_t)config_.length) active_step_ = 0;
}

void AcidSequencer::setTranspose(int16_t value) {
  config_.transpose = constrain((int)value, -24, 24);
}

void AcidSequencer::setNote(uint8_t index, int16_t note) {
  step(index).note = constrain((int)note, (int)kMutedNote, (int)kMaxNote);
}

void AcidSequencer::toggleAccent(uint8_t index) {
  step(index).flags ^= ACID_STEP_ACCENT;
}

void AcidSequencer::toggleSlide(uint8_t index) {
  step(index).flags ^= ACID_STEP_SLIDE;
}

void AcidSequencer::start() {
  playing_ = true;
  active_step_ = 0;
  next_step_us_ = micros() + internalStepPeriodUs(config_.bpm);
  publishCurrentStep();
}

void AcidSequencer::stopAndReset() {
  playing_ = false;
  active_step_ = -1;
  next_step_us_ = 0;
  last_external_edge_us_ = 0;
  external_period_us_ = 0;
  next_subdivision_us_ = 0;
  division_count_ = 0;
  subdivision_index_ = 0;
  publishStop();
}

void AcidSequencer::handleTransportClick() {
  if (externalClock()) {
    // Let the audio core reset its clock/subdivision state between samples.
    __atomic_store_n(&reset_requested_, 1u, __ATOMIC_RELEASE);
    return;
  }
  if (playing_) stopAndReset();
  else start();
}

void AcidSequencer::randomize(uint32_t seed) {
  if (seed == 0u) seed = 0x6d2b79f5u;
  auto nextRandom = [&seed]() -> uint32_t {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };

  // Twelve pitch classes fit in one mask; no per-note scale search is needed.
  static const uint16_t modes[] = {
      0xAB5u,  // Ionian
      0x6ADu,  // Dorian
      0x5ABu,  // Phrygian
      0xAD5u,  // Lydian
      0x6B5u,  // Mixolydian
      0x5ADu,  // Aeolian
      0x56Bu,  // Locrian
  };

  // Acid's note numbering starts at C1 = 0. Keep every generated note inside
  // the inclusive C1..C4 range while retaining modal and chromatic patterns.
  constexpr int8_t kRandomMinNote = 0;
  constexpr int8_t kRandomMaxNote = 36;
  const bool use_mode = (nextRandom() % 100u) < 68u;
  const uint8_t tonic = (uint8_t)(nextRandom() % 12u);
  const uint8_t mode = (uint8_t)(nextRandom() %
      (sizeof(modes) / sizeof(modes[0])));
  int8_t note_pool[kRandomMaxNote - kRandomMinNote + 1];
  uint8_t note_count = 0u;
  for (int8_t note = kRandomMinNote; note <= kRandomMaxNote; ++note) {
    const uint8_t interval = (uint8_t)((note - tonic + 24) % 12);
    if (!use_mode || (modes[mode] & (1u << interval)))
      note_pool[note_count++] = note;
  }

  for (uint8_t i = 0; i < kMaxSteps; ++i) {
    steps_[i].note = kMutedNote;
    steps_[i].flags = 0u;
  }

  const uint8_t density = (uint8_t)(48u + nextRandom() % 41u);
  const uint8_t accent_chance = (uint8_t)(15u + nextRandom() % 31u);
  const uint8_t slide_chance = (uint8_t)(10u + nextRandom() % 26u);
  int16_t pool_position = (int16_t)(nextRandom() % note_count);
  uint8_t active_steps = 0u;
  for (uint8_t i = 0; i < config_.length; ++i) {
    uint8_t step_density = density;
    if ((i & 3u) == 0u) step_density = min((uint8_t)100u,
                                           (uint8_t)(step_density + 12u));
    else if ((i & 1u) != 0u) step_density = max((uint8_t)20u,
                                                (uint8_t)(step_density - 8u));
    bool active = (nextRandom() % 100u) < step_density;
    if (i + 1u == config_.length && active_steps == 0u) active = true;
    if (!active) continue;

    if (active_steps == 0u || (nextRandom() % 100u) < 18u) {
      pool_position = (int16_t)(nextRandom() % note_count);
    } else {
      const int16_t movement = (int16_t)(nextRandom() % 7u) - 3;
      pool_position = constrain(pool_position + movement, 0,
                                (int16_t)note_count - 1);
    }
    steps_[i].note = note_pool[pool_position];
    if ((nextRandom() % 100u) < accent_chance)
      steps_[i].flags |= ACID_STEP_ACCENT;
    ++active_steps;
  }

  for (uint8_t i = 0; i < config_.length && config_.length > 1u; ++i) {
    const uint8_t next = (uint8_t)((i + 1u) % config_.length);
    if (steps_[i].note != kMutedNote && steps_[next].note != kMutedNote &&
        (nextRandom() % 100u) < slide_chance) {
      steps_[i].flags |= ACID_STEP_SLIDE;
    }
  }
  if (playing_ && active_step_ >= 0) publishCurrentStep();
}

void AcidSequencer::advanceStep() {
  if (!playing_) {
    playing_ = true;
    active_step_ = 0;
  } else {
    active_step_ = (int8_t)((active_step_ + 1) % config_.length);
  }
  publishCurrentStep();
}

uint32_t AcidSequencer::currentStepPeriodUs() const {
  if (!externalClock()) return internalStepPeriodUs(config_.bpm);
  const uint32_t clock_period = external_period_us_ == 0u
                                    ? 125000u
                                    : external_period_us_;
  switch (config_.ratio) {
    case ACID_RATIO_DIV2:
      return clock_period > UINT32_MAX / 2u ? UINT32_MAX : clock_period * 2u;
    case ACID_RATIO_X2: return max(1000u, clock_period / 2u);
    case ACID_RATIO_X4: return max(1000u, clock_period / 4u);
    default: return clock_period;
  }
}

void AcidSequencer::publishCurrentStep() {
  if (!playing_ || active_step_ < 0 || active_step_ >= (int8_t)config_.length) {
    publishStop();
    return;
  }
  const uint8_t index = (uint8_t)active_step_;
  const uint8_t previous = index == 0u ? config_.length - 1u : index - 1u;
  const AcidStep &current = steps_[index];
  const AcidStep &prior = steps_[previous];
  AcidSequenceEvent next;
  next.note = current.note == kMutedNote
                  ? kMutedNote
                  : (int8_t)constrain((int)current.note + config_.transpose,
                                     -24, 83);
  next.flags = current.flags;
  next.legato = current.note != kMutedNote && prior.note != kMutedNote &&
                 (prior.flags & ACID_STEP_SLIDE);
  next.playing = true;
  next.step_period_us = currentStepPeriodUs();

  __atomic_add_fetch(&event_revision_, 1u, __ATOMIC_ACQ_REL);
  event_.note = next.note;
  event_.flags = next.flags;
  event_.legato = next.legato;
  event_.playing = next.playing;
  event_.step_period_us = next.step_period_us;
  __atomic_add_fetch(&event_revision_, 1u, __ATOMIC_RELEASE);
}

void AcidSequencer::publishStop() {
  __atomic_add_fetch(&event_revision_, 1u, __ATOMIC_ACQ_REL);
  event_.note = kMutedNote;
  event_.flags = 0u;
  event_.legato = false;
  event_.playing = false;
  event_.step_period_us = 125000u;
  __atomic_add_fetch(&event_revision_, 1u, __ATOMIC_RELEASE);
}

void AcidSequencer::serviceInternal(uint32_t now_us) {
  if (!playing_ || (int32_t)(now_us - next_step_us_) < 0) return;
  advanceStep();
  const uint32_t period = internalStepPeriodUs(config_.bpm);
  // Skip missed deadlines in constant time instead of looping on the audio
  // core after a long interruption. Preserve the original clock phase.
  next_step_us_ += ((now_us - next_step_us_) / period + 1u) * period;
}

void AcidSequencer::acceptExternalEdge(uint32_t now_us) {
  if (last_external_edge_us_ != 0u) {
    const uint32_t measured = now_us - last_external_edge_us_;
    if (measured >= 1000u && measured <= kExternalTimeoutMaxUs)
      external_period_us_ = measured;
  }
  last_external_edge_us_ = now_us;
  if (config_.ratio == ACID_RATIO_DIV2 && ++division_count_ < 2u) return;
  division_count_ = 0;
  advanceStep();
  subdivision_index_ = 0;
  if (config_.ratio >= ACID_RATIO_X2 && external_period_us_ != 0u) {
    const uint8_t multiplier = config_.ratio == ACID_RATIO_X2 ? 2u : 4u;
    next_subdivision_us_ = now_us + external_period_us_ / multiplier;
    subdivision_index_ = 1u;
  }
}

void AcidSequencer::serviceExternal(uint32_t now_us) {
  const bool raw = PicoCVInputDigitalActiveLow(config_.clock - 1u);
  if (raw != raw_clock_) {
    raw_clock_ = raw;
    raw_changed_us_ = now_us;
  } else if (raw != stable_clock_ &&
             (now_us - raw_changed_us_) >= kClockDebounceUs) {
    const bool rising = raw && !stable_clock_;
    stable_clock_ = raw;
    if (rising) acceptExternalEdge(now_us);
  }

  if (playing_ && subdivision_index_ != 0u &&
      (int32_t)(now_us - next_subdivision_us_) >= 0) {
    const uint8_t multiplier = config_.ratio == ACID_RATIO_X2 ? 2u : 4u;
    advanceStep();
    if (++subdivision_index_ < multiplier)
      next_subdivision_us_ += external_period_us_ / multiplier;
    else
      subdivision_index_ = 0u;
  }

  if (!playing_ || last_external_edge_us_ == 0u) return;
  uint32_t timeout = external_period_us_ == 0u
                         ? kExternalTimeoutMaxUs
                         : external_period_us_ + external_period_us_ / 2u;
  timeout = constrain(timeout, kExternalTimeoutMinUs, kExternalTimeoutMaxUs);
  if ((now_us - last_external_edge_us_) > timeout) stopAndReset();
}

void AcidSequencer::serviceAudio() {
  if (++audio_service_divider_ < 4u) return;
  audio_service_divider_ = 0;
  if (__atomic_load_n(&reset_requested_, __ATOMIC_ACQUIRE) != 0u) {
    __atomic_exchange_n(&reset_requested_, 0u, __ATOMIC_ACQ_REL);
    if (externalClock()) stopAndReset();
  }
  const uint32_t now_us = micros();
  if (externalClock()) serviceExternal(now_us);
  else serviceInternal(now_us);
}
