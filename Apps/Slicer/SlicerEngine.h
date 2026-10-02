#ifndef PICOPRO_SLICER_ENGINE_H
#define PICOPRO_SLICER_ENGINE_H

#include <stdint.h>
#include "SlicerOctave.h"

enum class SlicerMode : uint8_t { Single, Tremolo, Harmonic };
enum class SlicerClock : uint8_t { Internal, CV1, CV2 };

struct SlicerParameters {
  float bpm = 120.0f;
  float attack = 0.1f;
  float duty = 0.5f;
  float mix = 1.0f;
  float depth = 1.0f;
  uint8_t pattern = 0;
  uint8_t ratio = 2;  // 0=/4, 1=/2, 2=x1, 3=x2, 4=x4.
  SlicerClock clock = SlicerClock::Internal;
  SlicerMode mode = SlicerMode::Single;
};

// One audio-core owner. Control and edge producers must hand off snapshots
// outside this class; there is no hardware, allocation or blocking here.
class SlicerEngine {
 public:
  static constexpr uint32_t kSampleRate = 44100;
  // A 30 BPM quarter-note pulse takes 2 s; leave margin for input jitter.
  static constexpr uint32_t kClockTimeoutMs = 3000;
  static constexpr uint8_t kPatternCount = 16;
  static constexpr uint8_t kSteps = 16;
  static constexpr uint8_t kRatioCount = 5;

  // One valid configuration before processing. I2S integer dividers can make
  // the physical stream rate differ from the nominal kSampleRate request.
  bool configureSampleRate(float hz);
  void setParameters(const SlicerParameters &parameters);
  // At x1, one physical pulse advances two steps; /4 advances half a step.
  // Timestamp is in sampleTime()'s modulo-uint32 domain. Future, duplicate
  // and short bounce edges are ignored.
  void clockEdge(uint32_t sample_time);
  // Capture ownership/epoch may change even if the latest control snapshot has
  // the same jack. Clear lock without rewinding emitted audio or sample time.
  void resetClockTracking();
  void process(float input_left, float input_right,
               float &output_left, float &output_right);

  uint32_t sampleTime() const { return sample_time_; }

 private:
  void advancePhase(float increment);
  float targetEnvelope(float step_frames, float gate_frames, float mapped_attack) const;

  SlicerParameters parameters_;
  bool initialized_ = false;
  bool running_ = true;
  bool clock_seen_ = false;
  bool sample_rate_locked_ = false;
  uint32_t sample_time_ = 0;
  uint32_t phase_ = 0;  // Q0.32 step phase, never reset on tempo/pattern changes.
  uint32_t last_edge_ = 0;
  uint8_t step_ = 0;
  float clock_grid_ = 0.0f;  // Fractional steps are required for the /4 ratio.
  float bpm_ = 120.0f;
  float attack_ = 0.1f;
  float duty_ = 0.5f;
  float mix_ = 1.0f;
  float depth_ = 1.0f;
  float envelope_ = 0.0f;
  float tremolo_amount_ = 0.0f;
  float harmonic_amount_ = 0.0f;
  float octave_up_ = 0.0f, octave_down_ = 0.0f;
  SlicerOctave octave_;
  float external_increment_ = 120.0f * 4.0f / (60.0f * kSampleRate);
  float clock_increment_ = 120.0f * 4.0f / (60.0f * kSampleRate);
  float phase_error_ = 0.0f;
  float sample_rate_ = kSampleRate;
  float bpm_to_increment_ = 4.0f / (60.0f * kSampleRate);
  float control_smoothing_ = 1.0f / (0.010f * kSampleRate + 1.0f);
  float clock_smoothing_ = 1.0f / (0.050f * kSampleRate + 1.0f);
  float minimum_ramp_ = 0.0005f * kSampleRate;
  float release_frames_ = 0.002f * kSampleRate;
  float release_increment_ = 1.0f / (0.002f * kSampleRate);
  uint32_t clock_timeout_ = kSampleRate * kClockTimeoutMs / 1000u;
  uint32_t minimum_edge_interval_ = kSampleRate / 20u;
};

#endif
