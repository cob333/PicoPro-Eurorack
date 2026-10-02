#include "SlicerEngine.h"

#include <math.h>

#include "SlicerPatterns.h"

namespace {

constexpr float kPhaseScale = 4294967296.0f;
constexpr float kPhaseInverse = 1.0f / kPhaseScale;

float clamp(float value, float low, float high) {
  // Comparisons also keep NaN control values out of the DSP state.
  if (!(value >= low)) return low;
  return value > high ? high : value;
}

float ratioMultiplier(uint8_t ratio) {
  // Relative to the internal sixteenth-note rate: external x1 is half speed.
  static const float factors[SlicerEngine::kRatioCount] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f};
  return factors[ratio];
}

float smooth(float value, float target, float coefficient, float tolerance) {
  if (value == target) return target;
  const float difference = target - value;
  const float next = value + difference * coefficient;
  // Float IIRs otherwise stall just short of one, or retain subnormal tails
  // near zero. Snap only after the ramp is inaudibly close or cannot advance.
  return fabsf(difference) <= tolerance || next == value ? target : next;
}

}  // namespace

bool SlicerEngine::configureSampleRate(float hz) {
  if (sample_rate_locked_ || clock_seen_ ||
      !isfinite(hz) || hz < 8000.0f || hz > 192000.0f) return false;
  const float previous_scale = bpm_to_increment_;
  sample_rate_ = hz;
  bpm_to_increment_ = 4.0f / (60.0f * hz);
  control_smoothing_ = 1.0f / (0.010f * hz + 1.0f);
  clock_smoothing_ = 1.0f / (0.050f * hz + 1.0f);
  minimum_ramp_ = 0.0005f * hz;
  release_frames_ = 0.002f * hz;
  release_increment_ = 1.0f / release_frames_;
  clock_timeout_ = static_cast<uint32_t>(hz * (kClockTimeoutMs * 0.001f) + 0.5f);
  minimum_edge_interval_ = static_cast<uint32_t>(hz * 0.050f + 0.5f);
  external_increment_ *= bpm_to_increment_ / previous_scale;
  clock_increment_ *= bpm_to_increment_ / previous_scale;
  sample_rate_locked_ = true;
  return true;
}

void SlicerEngine::setParameters(const SlicerParameters &parameters) {
  SlicerParameters next = parameters;
  next.bpm = clamp(next.bpm, 30.0f, 300.0f);
  next.attack = clamp(next.attack, 0.0f, 1.0f);
  next.duty = clamp(next.duty, 0.0f, 1.0f);
  next.mix = clamp(next.mix, 0.0f, 1.0f);
  next.depth = clamp(next.depth, 0.0f, 1.0f);
  if (next.pattern >= kPatternCount) next.pattern = kPatternCount - 1u;
  if (next.ratio >= kRatioCount) next.ratio = kRatioCount - 1u;
  if (static_cast<uint8_t>(next.clock) > static_cast<uint8_t>(SlicerClock::CV2))
    next.clock = SlicerClock::Internal;
  if (static_cast<uint8_t>(next.mode) > static_cast<uint8_t>(SlicerMode::Harmonic))
    next.mode = SlicerMode::Single;

  if (!initialized_) {
    bpm_ = next.bpm;
    attack_ = next.attack;
    duty_ = next.duty;
    mix_ = next.mix;
    depth_ = next.depth;
    tremolo_amount_ = next.mode == SlicerMode::Tremolo ? 1.0f : 0.0f;
    harmonic_amount_ = next.mode == SlicerMode::Harmonic ? 1.0f : 0.0f;
    external_increment_ = clock_increment_ = next.bpm * bpm_to_increment_ *
                                               ratioMultiplier(next.ratio);
    initialized_ = true;
  }
  if (next.clock != parameters_.clock) {
    clock_seen_ = false;
    phase_error_ = 0.0f;
  } else if (next.ratio != parameters_.ratio && clock_seen_) {
    clock_increment_ *= ratioMultiplier(next.ratio) /
                         ratioMultiplier(parameters_.ratio);
    // Changing a ratio changes speed, not the current sample phase.
    clock_grid_ = step_;
    phase_error_ = 0.0f;
  }
  parameters_ = next;
  running_ = next.clock == SlicerClock::Internal || clock_seen_;
}

void SlicerEngine::clockEdge(uint32_t sample_time) {
  if (parameters_.clock == SlicerClock::Internal ||
      static_cast<int32_t>(sample_time - sample_time_) > 0 ||
      sample_time_ - sample_time > clock_timeout_) return;

  if (!clock_seen_) {
    clock_seen_ = running_ = true;
    last_edge_ = sample_time;
    step_ = 0;
    clock_grid_ = 0.0f;
    phase_ = 0;
    phase_error_ = 0.0f;
    external_increment_ = clock_increment_ = bpm_ * bpm_to_increment_ *
                                               ratioMultiplier(parameters_.ratio);
    return;
  }

  const int32_t elapsed = static_cast<int32_t>(sample_time - last_edge_);
  if (elapsed < static_cast<int32_t>(minimum_edge_interval_)) return;
  last_edge_ = sample_time;
  const float period = clamp(static_cast<float>(elapsed),
      sample_rate_ * 60.0f / 300.0f, sample_rate_ * 60.0f / 30.0f);
  const float steps_per_edge = 4.0f * ratioMultiplier(parameters_.ratio);
  clock_increment_ = steps_per_edge / period;
  clock_grid_ += steps_per_edge;
  if (clock_grid_ >= kSteps) clock_grid_ -= kSteps;

  // Timestamp age is accounted for without rewinding an already emitted frame.
  const float target = clock_grid_ + (sample_time_ - sample_time) * clock_increment_;
  float error = target - (step_ + phase_ * kPhaseInverse);
  // modulo 16, shortest correction; no loop even after delayed delivery.
  error -= floorf((error + kSteps * 0.5f) / kSteps) * kSteps;
  phase_error_ = error;
}

void SlicerEngine::resetClockTracking() {
  clock_seen_ = false;
  phase_error_ = 0.0f;
  running_ = parameters_.clock == SlicerClock::Internal;
}

void SlicerEngine::advancePhase(float increment) {
  const uint32_t previous = phase_;
  phase_ += static_cast<uint32_t>(increment * kPhaseScale + 0.5f);
  if (phase_ < previous) step_ = (step_ + 1u) & (kSteps - 1u);
}

float SlicerEngine::targetEnvelope(float step_frames, float gate_frames,
                                   float mapped_attack) const {
  if (!running_ || duty_ <= 0.0f) return 0.0f;
  const float amplitude = slicer::kPatterns[parameters_.pattern][step_].gate;
  const float position = phase_ * kPhaseInverse;
  // Full duty leaves consecutive sounding steps joined; pattern transitions
  // still pass through the final bounded gain slew. Check this before comparing
  // phase: converting a Q0.32 value near wrap to float can round up to one.
  const bool full_duty = duty_ >= 0.99999f;
  if (!full_duty && position >= duty_) return 0.0f;
  float desired = 0.0f;
  // At steady TREMOLO there is no need to calculate the SINGLE envelope.
  if (tremolo_amount_ < 1.0f) {
    if (full_duty) {
      desired = amplitude;
    } else {
      const float attack_frames = fminf(mapped_attack, gate_frames * 0.5f);
      const float release_frames = fminf(release_frames_, gate_frames * 0.5f);
      const float time = position * step_frames;
      const float shape = fminf(time / fmaxf(attack_frames, 0.5f),
                           (gate_frames - time) / fmaxf(release_frames, 0.5f));
      desired = amplitude * clamp(shape, 0.0f, 1.0f);
    }
  }
  if (tremolo_amount_ > 0.0f) {
    float tremolo = 0.0f;
    if (position < duty_) {
      const float u = position / duty_;
      const float arch = 4.0f * u * (1.0f - u);
      // Polynomial pulse with zero slope at both edges; no per-sample trig.
      tremolo = amplitude * arch * arch;
    }
    if (tremolo_amount_ == 1.0f) return tremolo;
    desired += tremolo_amount_ * (tremolo - desired);
  }
  return desired;
}

void SlicerEngine::process(float input_left, float input_right,
                          float &output_left, float &output_right) {
  sample_rate_locked_ = true; // Also lock a default-rate stream, including counter wrap.
  bpm_ = smooth(bpm_, parameters_.bpm, control_smoothing_, 0.000001f);
  attack_ = smooth(attack_, parameters_.attack, control_smoothing_, 0.000001f);
  duty_ = smooth(duty_, parameters_.duty, control_smoothing_, 0.000001f);
  mix_ = smooth(mix_, parameters_.mix, control_smoothing_, 0.000001f);
  depth_ = smooth(depth_, parameters_.depth, control_smoothing_, 0.000001f);
  tremolo_amount_ = smooth(tremolo_amount_, parameters_.mode == SlicerMode::Tremolo ? 1.0f : 0.0f,
                            control_smoothing_, 0.000001f);
  harmonic_amount_ = smooth(harmonic_amount_, parameters_.mode == SlicerMode::Harmonic ? 1.0f : 0.0f,
                             control_smoothing_, 0.000001f);

  float increment = bpm_ * bpm_to_increment_;
  if (parameters_.clock != SlicerClock::Internal) {
    if (clock_seen_ && sample_time_ - last_edge_ > clock_timeout_) {
      clock_seen_ = running_ = false;
      phase_error_ = 0.0f;
    }
    external_increment_ = smooth(external_increment_, clock_increment_,
                                clock_smoothing_, 0.000000000001f);
    const float correction = clamp(phase_error_ * clock_smoothing_,
        -external_increment_ * 0.25f, external_increment_ * 0.25f);
    phase_error_ -= correction;
    increment = external_increment_ + correction;
  }

  const float step_frames = 1.0f / increment;
  const float mapped_attack = minimum_ramp_ + attack_ * (0.0295f * sample_rate_);
  const float gate_frames = duty_ * step_frames;
  const float desired = targetEnvelope(step_frames, gate_frames, mapped_attack);
  const float rise_frames = fmaxf(minimum_ramp_, fminf(mapped_attack, gate_frames));
  // This final slew also handles tiny gates, new patterns and clock timeouts.
  // A sub-ramp gate therefore has a reduced peak rather than an impulse.
  envelope_ += clamp(desired - envelope_, -release_increment_,
                                                1.0f / rise_frames);
  envelope_ = clamp(envelope_, 0.0f, 1.0f);
  input_left = isfinite(input_left) ? input_left : 0.0f;
  input_right = isfinite(input_right) ? input_right : 0.0f;
  octave_.push(input_left, input_right); // Warm fixed history even when bypassed.
  const int8_t pitch = slicer::kPatterns[parameters_.pattern][step_].pitch;
  octave_up_ = smooth(octave_up_, pitch == 12 ? 1.0f : 0.0f, control_smoothing_, 0.000001f);
  octave_down_ = fminf(smooth(octave_down_, pitch == -12 ? 1.0f : 0.0f,
                            control_smoothing_, 0.000001f), 1.0f - octave_up_);
  const float effect = mix_ * depth_;
  const float gain = 1.0f - effect * (1.0f - envelope_);
  output_left = input_left * gain;
  output_right = input_right * gain;
  if (harmonic_amount_ > 0.0f && effect > 0.0f && envelope_ > 0.0f &&
      (octave_up_ > 0.0f || octave_down_ > 0.0f)) {
    float shifted_left, shifted_right;
    octave_.blend(octave_up_, octave_down_, input_left, input_right, shifted_left, shifted_right);
    const float shifted_gain = effect * envelope_ * harmonic_amount_;
    output_left += shifted_gain * (shifted_left - input_left);
    output_right += shifted_gain * (shifted_right - input_right);
  }
  if (running_) advancePhase(increment);
  ++sample_time_;
}
