#ifndef PICOPRO_ADSR_H_
#define PICOPRO_ADSR_H_

#include <Arduino.h>

#define PICOPRO_ADSR_MAX 32767

enum PicoADSRStage {
  PICOPRO_ADSR_IDLE = 0,
  PICOPRO_ADSR_ATTACK,
  PICOPRO_ADSR_DECAY,
  PICOPRO_ADSR_SUSTAIN,
  PICOPRO_ADSR_RELEASE
};

struct PicoADSRCoeffs {
  uint16_t attack_inc;
  uint16_t decay_dec;
  uint16_t sustain;
  uint16_t release_dec;
};

struct PicoADSRRuntime {
  uint16_t value;
  uint8_t stage;
  bool gate;
};

struct PicoADSRParams {
  int16_t gate;
  int16_t attack;
  int16_t decay;
  int16_t sustain;
  int16_t release;
};

struct PicoADSRVoice {
  PicoADSRCoeffs coeffs;
  PicoADSRRuntime runtime;
  volatile bool enabled;
  volatile bool gate;
  volatile uint8_t gate_source;
  uint32_t last_gate_ms;
};

static inline int16_t PicoADSRClamp(int16_t value, int16_t max_value) {
  if (value < 0) return 0;
  if (value > max_value) return max_value;
  return value;
}

static inline uint16_t PicoADSRPercentToQ15(int16_t percent) {
  if (percent <= 0) return 0;
  if (percent >= 100) return PICOPRO_ADSR_MAX;
  return (uint16_t)(((uint32_t)percent * PICOPRO_ADSR_MAX) / 100u);
}

static inline uint16_t PicoADSRStepForMs(uint16_t distance, int16_t ms, uint32_t sample_rate) {
  if (distance == 0) return 1;
  if (ms <= 0 || sample_rate == 0) return distance;
  const uint32_t samples = ((uint32_t)ms * sample_rate) / 1000u;
  if (samples <= 1) return distance;
  uint32_t step = ((uint32_t)distance + samples - 1u) / samples;
  if (step == 0) step = 1;
  if (step > PICOPRO_ADSR_MAX) step = PICOPRO_ADSR_MAX;
  return (uint16_t)step;
}

static inline void PicoADSRBuildCoeffs(PicoADSRCoeffs *coeffs,
                                       int16_t attack_ms,
                                       int16_t decay_ms,
                                       int16_t sustain_percent,
                                       int16_t release_ms,
                                       uint32_t sample_rate) {
  const uint16_t sustain = PicoADSRPercentToQ15(sustain_percent);
  coeffs->sustain = sustain;
  coeffs->attack_inc = PicoADSRStepForMs(PICOPRO_ADSR_MAX, attack_ms, sample_rate);
  coeffs->decay_dec = PicoADSRStepForMs((uint16_t)(PICOPRO_ADSR_MAX - sustain), decay_ms, sample_rate);
  coeffs->release_dec = PicoADSRStepForMs(PICOPRO_ADSR_MAX, release_ms, sample_rate);
}

static inline void PicoADSRReset(PicoADSRRuntime *runtime) {
  runtime->value = 0;
  runtime->stage = PICOPRO_ADSR_IDLE;
  runtime->gate = false;
}

static inline void PicoADSRSetGate(PicoADSRRuntime *runtime, bool gate) {
  if (gate == runtime->gate) return;
  runtime->gate = gate;
  runtime->stage = gate ? PICOPRO_ADSR_ATTACK : PICOPRO_ADSR_RELEASE;
}

static inline uint16_t PicoADSRProcess(PicoADSRRuntime *runtime,
                                       const PicoADSRCoeffs *coeffs,
                                       bool enabled,
                                       bool gate) {
  if (!enabled) {
    PicoADSRReset(runtime);
    return PICOPRO_ADSR_MAX;
  }

  PicoADSRSetGate(runtime, gate);

  switch (runtime->stage) {
    case PICOPRO_ADSR_ATTACK: {
      const uint32_t next = (uint32_t)runtime->value + coeffs->attack_inc;
      if (next >= PICOPRO_ADSR_MAX) {
        runtime->value = PICOPRO_ADSR_MAX;
        runtime->stage = PICOPRO_ADSR_DECAY;
      } else {
        runtime->value = (uint16_t)next;
      }
      break;
    }
    case PICOPRO_ADSR_DECAY:
      if (runtime->value <= coeffs->sustain + coeffs->decay_dec) {
        runtime->value = coeffs->sustain;
        runtime->stage = PICOPRO_ADSR_SUSTAIN;
      } else {
        runtime->value -= coeffs->decay_dec;
      }
      break;
    case PICOPRO_ADSR_SUSTAIN:
      runtime->value = coeffs->sustain;
      break;
    case PICOPRO_ADSR_RELEASE:
      if (runtime->value <= coeffs->release_dec) {
        runtime->value = 0;
        runtime->stage = PICOPRO_ADSR_IDLE;
      } else {
        runtime->value -= coeffs->release_dec;
      }
      break;
    default:
      runtime->value = 0;
      runtime->stage = gate ? PICOPRO_ADSR_ATTACK : PICOPRO_ADSR_IDLE;
      break;
  }

  return runtime->value;
}

static inline void PicoADSRInitVoice(PicoADSRVoice *voice) {
  PicoADSRReset(&voice->runtime);
  voice->enabled = false;
  voice->gate = false;
  voice->gate_source = 0;
  voice->last_gate_ms = 0;
}

static inline void PicoADSRApplyParams(PicoADSRVoice *voice,
                                       int16_t gate_source,
                                       int16_t attack,
                                       int16_t decay,
                                       int16_t sustain,
                                       int16_t release,
                                       uint32_t sample_rate) {
  gate_source = PicoADSRClamp(gate_source, 2);
  attack = PicoADSRClamp(attack, 100);
  decay = PicoADSRClamp(decay, 100);
  sustain = PicoADSRClamp(sustain, 100);
  release = PicoADSRClamp(release, 100);

  PicoADSRBuildCoeffs(&voice->coeffs,
                      (int16_t)(attack * 20),
                      (int16_t)(decay * 20),
                      sustain,
                      (int16_t)(release * 30),
                      sample_rate);
  voice->gate_source = (uint8_t)gate_source;
  voice->enabled = gate_source > 0;
  if (gate_source == 0) {
    voice->gate = false;
  }
}

static inline void PicoADSRServiceGate(PicoADSRVoice *voice, uint32_t now_ms) {
  if ((now_ms - voice->last_gate_ms) < 2) {
    return;
  }
  voice->last_gate_ms = now_ms;

  const uint8_t source = voice->gate_source;
  if (source == 0) {
    voice->gate = false;
    return;
  }

  const uint16_t raw = source == 1 ? sampleCV1() : sampleCV2();
  const uint16_t value = (AD_RANGE - 1u) - raw;
  if (voice->gate) {
    if (value < (AD_RANGE / 3u)) {
      voice->gate = false;
    }
  } else if (value > ((AD_RANGE * 2u) / 3u)) {
    voice->gate = true;
  }
}

static inline int16_t PicoADSRApplyAmp(PicoADSRVoice *voice, int16_t level) {
  if (voice->enabled) {
    const uint16_t env = PicoADSRProcess(&voice->runtime,
                                         &voice->coeffs,
                                         true,
                                         voice->gate);
    return (int16_t)(((int32_t)level * env) >> 15);
  }
  PicoADSRProcess(&voice->runtime, &voice->coeffs, false, false);
  return level;
}

#endif // PICOPRO_ADSR_H_
