#ifndef PICOPRO_ACID_SEQUENCER_H_
#define PICOPRO_ACID_SEQUENCER_H_

#include <Arduino.h>

enum AcidClockSource : uint8_t {
  ACID_CLOCK_INTERNAL = 0,
  ACID_CLOCK_CV1 = 1,
  ACID_CLOCK_CV2 = 2,
};

enum AcidClockRatio : uint8_t {
  ACID_RATIO_DIV2 = 0,
  ACID_RATIO_X1 = 1,
  ACID_RATIO_X2 = 2,
  ACID_RATIO_X4 = 3,
};

enum AcidStepFlags : uint8_t {
  ACID_STEP_ACCENT = 1u << 0,
  ACID_STEP_SLIDE = 1u << 1,
};

struct AcidStep {
  int8_t note;
  uint8_t flags;
};

struct AcidSequenceConfig {
  uint16_t bpm;
  int8_t transpose;
  uint8_t clock;
  uint8_t ratio;
  uint8_t length;
};

struct AcidSequenceEvent {
  int8_t note;
  uint8_t flags;
  bool legato;
  bool playing;
  uint32_t step_period_us;
};

class AcidSequencer {
 public:
  static constexpr uint8_t kMaxSteps = 16u;
  static constexpr int8_t kMutedNote = -1;
  static constexpr int8_t kMinNote = 0;
  static constexpr int8_t kMaxNote = 59;

  void begin();
  void serviceAudio();
  void copyState(AcidSequenceConfig *config, AcidStep *steps) const;
  void restoreState(const AcidSequenceConfig &config, const AcidStep *steps);
  // revision is the caller's last consumed revision. Return true only for a
  // new valid snapshot; leave both outputs unchanged on contention/no change.
  bool readEvent(AcidSequenceEvent *event, uint32_t *revision) const;

  const AcidSequenceConfig &config() const { return config_; }
  AcidStep &step(uint8_t index) { return steps_[clampStep(index)]; }
  const AcidStep &step(uint8_t index) const { return steps_[clampStep(index)]; }

  void setClock(int16_t value);
  void setBpm(int16_t value);
  void setRatio(int16_t value);
  void setLength(int16_t value);
  void setTranspose(int16_t value);
  void setNote(uint8_t index, int16_t note);
  void toggleAccent(uint8_t index);
  void toggleSlide(uint8_t index);
  void handleTransportClick();
  void randomize(uint32_t seed);

  bool playing() const { return playing_; }
  bool externalClock() const { return config_.clock != ACID_CLOCK_INTERNAL; }
  int8_t activeStep() const { return active_step_; }

 private:
  uint8_t clampStep(uint8_t index) const {
    return index < kMaxSteps ? index : (kMaxSteps - 1u);
  }
  void resetDefaults();
  void sanitize();
  void updateDigitalRole();
  void start();
  void stopAndReset();
  void advanceStep();
  void publishCurrentStep();
  void publishStop();
  uint32_t currentStepPeriodUs() const;
  void serviceInternal(uint32_t now_us);
  void serviceExternal(uint32_t now_us);
  void acceptExternalEdge(uint32_t now_us);

  AcidSequenceConfig config_ = {120u, 0, ACID_CLOCK_INTERNAL,
                                ACID_RATIO_X1, kMaxSteps};
  AcidStep steps_[kMaxSteps] = {};
  volatile bool playing_ = false;
  volatile int8_t active_step_ = -1;
  int8_t digital_clock_input_ = 0;
  uint32_t next_step_us_ = 0;
  uint32_t last_external_edge_us_ = 0;
  uint32_t external_period_us_ = 0;
  uint32_t raw_changed_us_ = 0;
  uint32_t next_subdivision_us_ = 0;
  bool raw_clock_ = false;
  bool stable_clock_ = false;
  uint8_t division_count_ = 0;
  uint8_t subdivision_index_ = 0;
  uint8_t audio_service_divider_ = 0;
  uint32_t reset_requested_ = 0;
  volatile uint32_t event_revision_ = 0;
  volatile AcidSequenceEvent event_ = {
      kMutedNote, 0u, false, false, 125000u};
};

#endif
