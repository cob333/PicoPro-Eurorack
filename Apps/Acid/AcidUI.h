#ifndef PICOPRO_ACID_UI_H_
#define PICOPRO_ACID_UI_H_

#include <Adafruit_SSD1306.h>
#include "ClickEncoder.h"
#include "AcidBass.h"
#include "AcidSequencer.h"

class AcidUI {
 public:
  AcidUI(Adafruit_SSD1306 &display, ClickEncoder &encoder,
         AcidSequencer &sequencer, AcidBass &bass)
      : display_(display), encoder_(encoder), sequencer_(sequencer), bass_(bass) {}

  void begin();
  void service();

 private:
  enum Page : uint8_t { ROOT, SEQ, STEPS, BASS };
  enum RootItem : uint8_t { ROOT_SEQ, ROOT_BASS };
  enum SeqItem : uint8_t {
    SEQ_CLOCK,
    SEQ_TIMING,
    SEQ_LENGTH,
    SEQ_EDIT_STEPS,
    SEQ_TRANSPOSE,
    SEQ_BACK,
    SEQ_ITEM_COUNT
  };
  enum BassItem : uint8_t {
    BASS_FREQ,
    BASS_WAVE,
    BASS_CUTOFF,
    BASS_RESONANCE,
    BASS_ENV_MOD,
    BASS_DECAY,
    BASS_ACCENT,
    BASS_SLIDE,
    BASS_DRIVE,
    BASS_LEVEL,
    BASS_BACK,
    BASS_ITEM_COUNT
  };
  enum StepGesture : uint8_t {
    STEP_GESTURE_NONE,
    STEP_GESTURE_SINGLE,
    STEP_GESTURE_DOUBLE,
    STEP_GESTURE_TRIPLE,
  };
  enum MenuState : uint8_t {
    MENU_SELECT,
    MENU_WAIT_ENTER_RELEASE,
    MENU_EDITING,
    MENU_WAIT_EXIT_RELEASE,
  };

  void serviceRoot(int16_t movement, bool button_down);
  void serviceSeq(int16_t movement, bool button_down);
  void serviceSteps(int16_t movement);
  void serviceBass(int16_t movement, bool button_down);
  void serviceMenu(int16_t movement, bool button_down, uint8_t &item,
                   uint8_t count, MenuState &state, bool &editing,
                   bool editable);
  void applyMenuMovement(int16_t movement);
  void activateMenuItem();
  void applySeqMovement(int16_t movement);
  void applyBassMovement(int16_t movement);
  void activateSeqItem();
  int16_t scaledMovement(int16_t movement) const;
  StepGesture serviceStepGesture(uint32_t now_ms);
  void resetStepGesture();
  void resetRootGesture();

  void draw();
  void drawRoot();
  void drawSeq();
  void drawSteps();
  void drawBass();
  void drawKnob(int16_t value, int16_t min_value, int16_t max_value);
  void drawNavArrows();
  void drawCentered(const char *text, int16_t y);
  void drawPageIndex(uint8_t index, uint8_t total);
  void drawTransportStatus();
  void drawPlayAnimation(bool mouth_open);
  void drawBottomLabel(const char *text, bool selected = false);
  void drawKnobLabel(const char *top, const char *bottom);
  void drawTinyNote(int16_t x, int16_t y, int8_t note);
  void drawStepCell(uint8_t index, int16_t x);

  Adafruit_SSD1306 &display_;
  ClickEncoder &encoder_;
  AcidSequencer &sequencer_;
  AcidBass &bass_;
  Page page_ = ROOT;
  uint8_t root_item_ = ROOT_SEQ;
  uint8_t seq_item_ = SEQ_CLOCK;
  bool editing_seq_ = false;
  MenuState seq_menu_state_ = MENU_SELECT;
  uint8_t bass_item_ = BASS_FREQ;
  bool editing_bass_ = false;
  MenuState bass_menu_state_ = MENU_SELECT;
  int16_t button_debounce_counter_ = 0;
  int8_t selected_step_ = 0;
  bool editing_step_ = false;
  bool display_dirty_ = false;
  uint32_t play_animation_ms_ = 0;
  uint8_t play_animation_frame_ = 0;
  bool root_overlay_visible_ = false;
  uint32_t root_overlay_ms_ = 0;
  bool root_button_raw_ = false;
  bool root_button_stable_ = false;
  bool root_button_blocked_ = true;
  bool root_button_cancelled_ = false;
  bool root_click_pending_ = false;
  bool root_second_click_ = false;
  uint32_t root_button_changed_ms_ = 0;
  uint32_t root_button_pressed_ms_ = 0;
  uint32_t root_click_deadline_ms_ = 0;
  uint32_t root_release_ready_ms_ = 0;
  bool mouth_animation_active_ = false;
  uint32_t mouth_animation_started_ms_ = 0;
  int8_t last_playhead_ = -2;
  bool last_transport_running_ = false;
  bool gesture_raw_ = false;
  bool gesture_stable_ = false;
  uint32_t gesture_changed_ms_ = 0;
  uint32_t gesture_pressed_ms_ = 0;
  uint32_t gesture_deadline_ms_ = 0;
  uint8_t gesture_clicks_ = 0;
  bool gesture_cancelled_ = false;
  bool gesture_started_editing_ = false;
};

#endif
