#include "AcidUI.h"

#include "AcidCV.h"
#include "PicoPro_io.h"
#include <../Fonts/Picopixel.h>

namespace {

constexpr uint32_t kDisplaySleepMs = 60000u;
constexpr uint32_t kGestureDebounceMs = 15u;
constexpr uint32_t kGestureClickMinMs = 20u;
constexpr uint32_t kGestureClickMaxMs = 350u;
constexpr uint32_t kGestureWindowMs = 280u;
constexpr uint32_t kRootSecondPressMs = 150u;
constexpr uint32_t kPlayAnimationMs = 45u;
constexpr uint32_t kRootOverlayMs = 1400u;
constexpr uint32_t kMouthAnimationMs = 900u;
constexpr int16_t kButtonDebounceCycles = 10;

const char *const kClockNames[] = {"int", "cv1", "cv2"};
const char *const kRatioNames[] = {"/2", "x1", "x2", "x4"};
const char *const kWaveNames[] = {"saw", "square", "tri"};
const char *const kPitchNames[] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"};

const int8_t kNeedleX[21] = {
    -6, -7, -7, -7, -6, -5, -4, -3, -2, -1, 0,
     1,  2,  3,  4,  5,  6,  7,  7,  7, 6};
const int8_t kNeedleY[21] = {
     3,  2,  1,  0, -3, -4, -5, -6, -7, -7, -7,
    -7, -7, -6, -5, -4, -3,  0,  1,  2, 3};

const int8_t kFaceCosQ7[32] = {
    127, 125, 117, 106, 90, 71, 49, 25,
    0, -25, -49, -71, -90, -106, -117, -125,
    -127, -125, -117, -106, -90, -71, -49, -25,
    0, 25, 49, 71, 90, 106, 117, 125};
const int8_t kFaceSinQ7[32] = {
    0, 25, 49, 71, 90, 106, 117, 125,
    127, 125, 117, 106, 90, 71, 49, 25,
    0, -25, -49, -71, -90, -106, -117, -125,
    -127, -125, -117, -106, -90, -71, -49, -25};
const int8_t kFaceFeatureX[] = {
    -4, -3, -4, -3,  3,  4,  3,  4,
    -4, -3, -2, -1,  0,  1,  2,  3,  4,
    -4, -3, -2, -1,  0,  1,  2,  3,  4,
    -6, -6,  6,  6};
const int8_t kFaceFeatureY[] = {
    -4, -4, -3, -3, -4, -4, -3, -3,
     2,  4,  5,  5,  5,  5,  5,  4,  2,
     1,  3,  4,  4,  4,  4,  4,  3,  1,
     1,  2,  1,  2};
static_assert(sizeof(kFaceFeatureX) == sizeof(kFaceFeatureY),
              "Face feature coordinate mismatch");

int16_t q7Round(int16_t value) {
  return value >= 0 ? (value + 63) / 127 : (value - 63) / 127;
}

uint8_t wrapIndex(int16_t value, uint8_t count) {
  while (value < 0) value += count;
  while (value >= count) value -= count;
  return (uint8_t)value;
}

}  // namespace

void AcidUI::begin() {
  display_.clearDisplay();
  display_.setTextSize(1);
  display_.setTextColor(WHITE, BLACK);
  encoder_.setAccelerationEnabled(false);
  encoder_.getValue();
  encoder_.getButton();
  last_activity_ms_ = millis();
  last_transport_running_ = sequencer_.playing();
  resetRootGesture();
  draw();
  display_.display();
  display_dirty_ = false;
}

void AcidUI::noteActivity() {
  last_activity_ms_ = millis();
  if (display_asleep_) {
    display_asleep_ = false;
    draw();
  }
}

void AcidUI::service() {
  const int16_t movement = encoder_.getValue();
  const ClickEncoder::Button button = encoder_.getButton();
  const bool button_down = digitalRead(ENCSW_IN) == LOW;

  if (movement != 0 || button != ClickEncoder::Open || button_down)
    noteActivity();
  if (display_asleep_) return;

  if (page_ == BASS) {
    const uint8_t cv_result = AcidCVServiceOverlay(
        movement, button_down, button);
    if (cv_result != 0u) {
      if (cv_result == 2u) {
        bass_menu_state_ = MENU_SELECT;
        editing_bass_ = false;
        draw();
        display_.display();
        display_dirty_ = false;
      }
      return;
    }
    if (bass_item_ < BASS_BACK && AcidCVHandleEntry(button, bass_item_)) {
      bass_menu_state_ = MENU_SELECT;
      editing_bass_ = false;
      return;
    }
  }

  switch (page_) {
    case ROOT: serviceRoot(movement, button_down); break;
    case SEQ: serviceSeq(movement, button_down); break;
    case STEPS: serviceSteps(movement); break;
    case BASS: serviceBass(movement, button_down); break;
  }

  const int8_t playhead = sequencer_.activeStep();
  const bool transport_running = sequencer_.playing();
  if (transport_running != last_transport_running_) {
    last_transport_running_ = transport_running;
    if (page_ == ROOT || page_ == SEQ || page_ == STEPS) draw();
  }
  if (page_ == STEPS && playhead != last_playhead_) {
    last_playhead_ = playhead;
    draw();
  }
  const uint32_t now_ms = millis();
  if (page_ == ROOT && root_overlay_visible_ &&
      (now_ms - root_overlay_ms_) >= kRootOverlayMs) {
    root_overlay_visible_ = false;
    draw();
  }
  if (mouth_animation_active_ &&
      (now_ms - mouth_animation_started_ms_) >= kMouthAnimationMs) {
    mouth_animation_active_ = false;
    if (page_ == ROOT) draw();
  }
  if (page_ == ROOT && (transport_running || mouth_animation_active_) &&
      (now_ms - play_animation_ms_) >= kPlayAnimationMs) {
    play_animation_ms_ = now_ms;
    play_animation_frame_ = (play_animation_frame_ + 1u) % 160u;
    draw();
  }
  if (page_ == BASS && bass_item_ < BASS_BACK &&
      AcidCVShouldRefresh(bass_item_, now_ms)) {
    draw();
  }
  if ((now_ms - last_activity_ms_) >= kDisplaySleepMs) {
    display_.clearDisplay();
    display_.display();
    display_asleep_ = true;
    display_dirty_ = false;
  } else if (display_dirty_) {
    display_.display();
    display_dirty_ = false;
  }
}

void AcidUI::resetRootGesture() {
  const uint32_t now_ms = millis();
  const bool button_down = digitalRead(ENCSW_IN) == LOW;
  root_button_raw_ = button_down;
  root_button_stable_ = button_down;
  root_button_blocked_ = true;
  root_button_cancelled_ = false;
  root_click_pending_ = false;
  root_second_click_ = false;
  root_button_changed_ms_ = now_ms;
  root_button_pressed_ms_ = button_down ? now_ms : 0u;
  root_click_deadline_ms_ = 0u;
  root_release_ready_ms_ = now_ms + kGestureDebounceMs;
}

void AcidUI::serviceRoot(int16_t movement, bool button_down) {
  const uint32_t now_ms = millis();
  if (root_button_blocked_) {
    if (button_down) {
      root_release_ready_ms_ = now_ms + kGestureDebounceMs;
    } else if ((int32_t)(now_ms - root_release_ready_ms_) >= 0) {
      root_button_blocked_ = false;
      root_button_raw_ = false;
      root_button_stable_ = false;
      root_button_changed_ms_ = now_ms;
    }
    return;
  }

  if (movement != 0 && !button_down) {
    root_click_pending_ = false;
    root_item_ = wrapIndex((int16_t)root_item_ + (movement > 0 ? 1 : -1), 2u);
    root_overlay_visible_ = true;
    root_overlay_ms_ = now_ms;
    draw();
  } else if (movement != 0) {
    root_button_cancelled_ = true;
    root_click_pending_ = false;
    root_second_click_ = false;
  }

  if (button_down != root_button_raw_) {
    root_button_raw_ = button_down;
    root_button_changed_ms_ = now_ms;
    if (button_down) root_button_cancelled_ = false;
    // Recognize the start of the second press immediately. The release is
    // still debounced before the double-click action is accepted.
    if (button_down && root_click_pending_ &&
        (int32_t)(now_ms - root_click_deadline_ms_) < 0) {
      root_click_pending_ = false;
      root_second_click_ = true;
    }
  } else if (button_down != root_button_stable_ &&
             (now_ms - root_button_changed_ms_) >= kGestureDebounceMs) {
    root_button_stable_ = button_down;
    if (button_down) {
      root_button_pressed_ms_ = now_ms;
    } else {
      const uint32_t duration = now_ms - root_button_pressed_ms_;
      if (!root_button_cancelled_ && duration >= kGestureClickMinMs &&
          duration <= kGestureClickMaxMs) {
        if (root_second_click_) {
          sequencer_.randomize(
              micros() ^ ((uint32_t)play_animation_frame_ << 24));
          root_overlay_visible_ = false;
          mouth_animation_active_ = true;
          mouth_animation_started_ms_ = now_ms;
          play_animation_ms_ = now_ms;
          draw();
        } else if (root_overlay_visible_) {
          root_overlay_visible_ = false;
          if (root_item_ == ROOT_SEQ) {
            page_ = SEQ;
            seq_item_ = SEQ_CLOCK;
            editing_seq_ = false;
            seq_menu_state_ = MENU_SELECT;
          } else {
            page_ = BASS;
            bass_item_ = BASS_FREQ;
            editing_bass_ = false;
            bass_menu_state_ = MENU_SELECT;
          }
          draw();
          return;
        } else {
          root_click_pending_ = true;
          root_click_deadline_ms_ = now_ms + kRootSecondPressMs;
        }
      }
      root_second_click_ = false;
      root_button_cancelled_ = false;
    }
  }

  if (root_click_pending_ &&
      (int32_t)(now_ms - root_click_deadline_ms_) >= 0) {
    root_click_pending_ = false;
    sequencer_.handleTransportClick();
    draw();
  }
}

int16_t AcidUI::scaledMovement(int16_t movement) const {
  return movement != 0 && encoder_.isPressRotateLocked()
             ? movement * encoder_.getPressRotateMultiplier()
             : movement;
}

void AcidUI::serviceBass(int16_t movement, bool button_down) {
  serviceMenu(movement, button_down, bass_item_, BASS_ITEM_COUNT,
              bass_menu_state_, editing_bass_, bass_item_ != BASS_BACK);
}

void AcidUI::applyMenuMovement(int16_t movement) {
  if (page_ == BASS) applyBassMovement(movement);
  else applySeqMovement(movement);
}

void AcidUI::activateMenuItem() {
  if (page_ == BASS) {
    page_ = ROOT;
    root_item_ = ROOT_BASS;
    root_overlay_visible_ = false;
    resetRootGesture();
  } else {
    activateSeqItem();
  }
}

void AcidUI::serviceMenu(int16_t movement, bool button_down, uint8_t &item,
                          uint8_t count, MenuState &state, bool &editing,
                          bool editable) {
  switch (state) {
    case MENU_SELECT:
      if (movement != 0 && !button_down) {
        item = wrapIndex((int16_t)item + (movement > 0 ? 1 : -1), count);
        editable = page_ == BASS ? item != BASS_BACK :
            (item == SEQ_CLOCK || item == SEQ_TIMING ||
             item == SEQ_LENGTH || item == SEQ_TRANSPOSE);
        draw();
      }
      if (button_down) {
        editing = editable;
        if (editable && movement != 0 &&
            encoder_.isPressRotateLocked()) {
          state = MENU_EDITING;
          applyMenuMovement(scaledMovement(movement));
        } else {
          button_debounce_counter_ = kButtonDebounceCycles;
          state = MENU_WAIT_ENTER_RELEASE;
        }
        draw();
      }
      break;
    case MENU_WAIT_ENTER_RELEASE:
      if (editable && (movement != 0 || encoder_.isPressRotateLocked())) {
        state = MENU_EDITING;
        if (movement != 0) applyMenuMovement(scaledMovement(movement));
        draw();
      } else if (!button_down && --button_debounce_counter_ <= 0) {
        if (!editable) {
          activateMenuItem();
          state = MENU_SELECT;
          editing = false;
        } else {
          state = MENU_EDITING;
        }
        draw();
      }
      break;
    case MENU_EDITING:
      if (movement != 0) {
        applyMenuMovement(scaledMovement(movement));
        draw();
      }
      if (button_down && !encoder_.isPressRotateLocked()) {
        button_debounce_counter_ = kButtonDebounceCycles;
        state = MENU_WAIT_EXIT_RELEASE;
      }
      break;
    case MENU_WAIT_EXIT_RELEASE:
      if (movement != 0 || encoder_.isPressRotateLocked()) {
        state = MENU_EDITING;
        if (movement != 0) applyMenuMovement(scaledMovement(movement));
        draw();
      } else if (!button_down && --button_debounce_counter_ <= 0) {
        state = MENU_SELECT;
        editing = false;
        draw();
      }
      break;
  }
}

void AcidUI::applyBassMovement(int16_t movement) {
  const AcidBassConfig &config = bass_.config();
  switch (bass_item_) {
    case BASS_FREQ: bass_.setFrequency((int32_t)config.frequency + movement); break;
    case BASS_WAVE: bass_.setWave((int16_t)config.wave + movement); break;
    case BASS_CUTOFF: bass_.setCutoff((int32_t)config.cutoff + movement * 10); break;
    case BASS_RESONANCE:
      bass_.setResonance((int16_t)config.resonance + movement * 10);
      break;
    case BASS_ENV_MOD:
      bass_.setEnvMod((int16_t)config.env_mod + movement * 10);
      break;
    case BASS_DECAY:
      bass_.setDecay((int32_t)config.decay_ms + movement * 10);
      break;
    case BASS_ACCENT:
      bass_.setAccent((int16_t)config.accent + movement * 10);
      break;
    case BASS_SLIDE:
      bass_.setSlide((int16_t)config.slide_ms + movement);
      break;
    case BASS_DRIVE:
      bass_.setDrive((int16_t)config.drive + movement * 10);
      break;
    case BASS_LEVEL:
      bass_.setLevel((int16_t)config.level + movement * 10);
      break;
    default: break;
  }
}

void AcidUI::activateSeqItem() {
  if (seq_item_ == SEQ_BACK) {
    page_ = ROOT;
    root_overlay_visible_ = false;
    resetRootGesture();
  } else if (seq_item_ == SEQ_EDIT_STEPS) {
    page_ = STEPS;
    selected_step_ = min(selected_step_,
                         (int8_t)(sequencer_.config().length - 1u));
    editing_step_ = false;
    resetStepGesture();
  }
}

void AcidUI::serviceSeq(int16_t movement, bool button_down) {
  const bool editable = seq_item_ == SEQ_CLOCK || seq_item_ == SEQ_TIMING ||
                        seq_item_ == SEQ_LENGTH || seq_item_ == SEQ_TRANSPOSE;
  serviceMenu(movement, button_down, seq_item_, SEQ_ITEM_COUNT,
              seq_menu_state_, editing_seq_, editable);
}

void AcidUI::applySeqMovement(int16_t movement) {
  const AcidSequenceConfig &config = sequencer_.config();
  switch (seq_item_) {
    case SEQ_CLOCK: sequencer_.setClock(config.clock + movement); break;
    case SEQ_TIMING:
      if (config.clock == ACID_CLOCK_INTERNAL)
        sequencer_.setBpm((int16_t)config.bpm + movement);
      else
        sequencer_.setRatio(config.ratio + movement);
      break;
    case SEQ_LENGTH: sequencer_.setLength(config.length + movement); break;
    case SEQ_TRANSPOSE: sequencer_.setTranspose(config.transpose + movement); break;
    default: break;
  }
}

void AcidUI::resetStepGesture() {
  gesture_raw_ = digitalRead(ENCSW_IN) == LOW;
  gesture_stable_ = gesture_raw_;
  gesture_changed_ms_ = millis();
  gesture_pressed_ms_ = gesture_raw_ ? gesture_changed_ms_ : 0u;
  gesture_deadline_ms_ = 0;
  gesture_clicks_ = 0;
  gesture_cancelled_ = false;
  gesture_started_editing_ = false;
}

AcidUI::StepGesture AcidUI::serviceStepGesture(uint32_t now_ms) {
  const bool raw = digitalRead(ENCSW_IN) == LOW;
  if (gesture_cancelled_) {
    gesture_raw_ = raw;
    gesture_stable_ = raw;
    gesture_changed_ms_ = now_ms;
    gesture_clicks_ = 0;
    if (!raw) gesture_cancelled_ = false;
    return STEP_GESTURE_NONE;
  }
  if (raw != gesture_raw_) {
    gesture_raw_ = raw;
    gesture_changed_ms_ = now_ms;
  } else if (raw != gesture_stable_ &&
             (now_ms - gesture_changed_ms_) >= kGestureDebounceMs) {
    gesture_stable_ = raw;
    if (raw) {
      gesture_pressed_ms_ = now_ms;
    } else {
      const uint32_t duration = now_ms - gesture_pressed_ms_;
      if (duration >= kGestureClickMinMs && duration <= kGestureClickMaxMs) {
        ++gesture_clicks_;
        gesture_deadline_ms_ = now_ms + kGestureWindowMs;
        if (gesture_clicks_ >= 3u) {
          gesture_clicks_ = 0;
          return STEP_GESTURE_TRIPLE;
        }
        return gesture_clicks_ == 1u ? STEP_GESTURE_SINGLE
                                     : STEP_GESTURE_DOUBLE;
      } else {
        gesture_clicks_ = 0;
      }
    }
  }
  if (gesture_clicks_ != 0u &&
      (int32_t)(now_ms - gesture_deadline_ms_) >= 0) {
    gesture_clicks_ = 0;
    gesture_started_editing_ = false;
  }
  return STEP_GESTURE_NONE;
}

void AcidUI::serviceSteps(int16_t movement) {
  const bool press_rotate = encoder_.isPressRotateLocked();
  if (press_rotate) {
    gesture_cancelled_ = true;
    gesture_clicks_ = 0;
  }
  if (movement != 0) {
    if (editing_step_ || (press_rotate && selected_step_ >= 0)) {
      editing_step_ = true;
      sequencer_.setNote(selected_step_,
                         sequencer_.step(selected_step_).note +
                             scaledMovement(movement));
    } else {
      const int8_t length = (int8_t)sequencer_.config().length;
      const int8_t count = length + 1;
      int8_t position = selected_step_ < 0 ? length : selected_step_;
      position = (int8_t)wrapIndex(position + (movement > 0 ? 1 : -1), count);
      selected_step_ = position == length ? -1 : position;
    }
    draw();
  }

  const StepGesture gesture = serviceStepGesture(millis());
  if (gesture == STEP_GESTURE_NONE) return;
  if (gesture == STEP_GESTURE_SINGLE) {
    gesture_started_editing_ = editing_step_;
    if (selected_step_ < 0) {
      page_ = SEQ;
      seq_item_ = SEQ_EDIT_STEPS;
      seq_menu_state_ = MENU_SELECT;
    } else {
      editing_step_ = !editing_step_;
    }
  } else if (gesture == STEP_GESTURE_DOUBLE &&
             gesture_started_editing_ && selected_step_ >= 0) {
    editing_step_ = true;
    sequencer_.toggleAccent(selected_step_);
  } else if (gesture == STEP_GESTURE_TRIPLE &&
             gesture_started_editing_ && selected_step_ >= 0) {
    editing_step_ = true;
    // The second release tentatively applies a double-click immediately;
    // undo it when a third release converts the gesture into a slide toggle.
    sequencer_.toggleAccent(selected_step_);
    sequencer_.toggleSlide(selected_step_);
  }
  draw();
}

void AcidUI::drawCentered(const char *text, int16_t y) {
  const int16_t width = strlen(text) * 6;
  display_.setCursor(max(0, (64 - width) / 2), y);
  display_.print(text);
}

void AcidUI::drawPageIndex(uint8_t index, uint8_t total) {
  char label[8];
  snprintf(label, sizeof(label), "%u/%u", index + 1u, total);
  int16_t x1, y1;
  uint16_t text_width, text_height;
  display_.setFont(&Picopixel);
  display_.getTextBounds(label, 0, 0, &x1, &y1, &text_width, &text_height);
  const uint8_t box_width = (uint8_t)text_width + 4u;
  display_.fillRect(0, 0, box_width, 9, BLACK);
  display_.drawRect(0, 0, box_width, 9, WHITE);
  display_.setTextColor(WHITE, BLACK);
  display_.setCursor(2, 6);
  display_.print(label);
  display_.setFont(NULL);
}

void AcidUI::drawTransportStatus() {
  display_.fillRect(55, 0, 9, 9, BLACK);
  if (sequencer_.playing()) {
    display_.fillTriangle(57, 1, 57, 7, 62, 4, WHITE);
  } else {
    display_.fillRect(57, 2, 6, 6, WHITE);
  }
}

void AcidUI::drawPlayAnimation(bool mouth_open) {
  constexpr int16_t kCenterX = 32;
  constexpr int16_t kCenterY = 16;
  const uint8_t rotation = play_animation_frame_ & 31u;
  const uint8_t phase = (play_animation_frame_ >> 1) % 20u;
  for (uint8_t offset = 0; offset < 20u; offset += 5u) {
    const uint8_t radius = 12u + ((phase + offset) % 20u);
    display_.drawCircle(kCenterX, kCenterY, radius, WHITE);
    display_.drawCircle(kCenterX, kCenterY, radius + 1u, WHITE);
  }
  display_.fillCircle(kCenterX, kCenterY, 10, BLACK);
  display_.drawCircle(kCenterX, kCenterY, 9, WHITE);
  display_.drawCircle(kCenterX, kCenterY, 10, WHITE);

  const int8_t cosine = kFaceCosQ7[rotation];
  const int8_t sine = kFaceSinQ7[rotation];
  for (uint8_t i = 0; i < sizeof(kFaceFeatureX); ++i) {
    if (mouth_open && i >= 8u && i <= 25u) continue;
    const int16_t rotated_x = q7Round(
        (int16_t)kFaceFeatureX[i] * cosine -
        (int16_t)kFaceFeatureY[i] * sine);
    const int16_t rotated_y = q7Round(
        (int16_t)kFaceFeatureX[i] * sine +
        (int16_t)kFaceFeatureY[i] * cosine);
    display_.drawPixel(kCenterX + rotated_x, kCenterY + rotated_y, WHITE);
  }
  if (mouth_open) {
    const int16_t mouth_x = kCenterX + q7Round(-4 * sine);
    const int16_t mouth_y = kCenterY + q7Round(4 * cosine);
    display_.drawCircle(mouth_x, mouth_y, 3, WHITE);
    display_.drawCircle(mouth_x, mouth_y, 2, WHITE);
  }
}

void AcidUI::drawBottomLabel(const char *text, bool selected) {
  if (selected) {
    display_.fillRect(0, 23, 64, 9, WHITE);
    display_.setTextColor(BLACK, WHITE);
  } else {
    display_.setTextColor(WHITE, BLACK);
  }
  drawCentered(text, 24);
  display_.setTextColor(WHITE, BLACK);
}

void AcidUI::drawKnobLabel(const char *top, const char *bottom) {
  drawCentered(top, 7);
  drawCentered(bottom, 16);
}

void AcidUI::drawNavArrows() {
  display_.drawLine(7, 13, 4, 16, WHITE);
  display_.drawLine(4, 16, 7, 19, WHITE);
  display_.drawLine(56, 13, 59, 16, WHITE);
  display_.drawLine(59, 16, 56, 19, WHITE);
}

void AcidUI::drawKnob(int16_t value, int16_t min_value, int16_t max_value) {
  const int32_t span = max_value - min_value;
  const uint8_t index = span <= 0 ? 0u : (uint8_t)constrain(
      ((int32_t)(value - min_value) * 20 + span / 2) / span, 0, 20);
  display_.drawCircle(32, 10, 8, WHITE);
  display_.drawPixel(32, 10, WHITE);
  display_.drawLine(32, 10, 32 + kNeedleX[index], 10 + kNeedleY[index], WHITE);
  drawNavArrows();
}

void AcidUI::drawRoot() {
  display_.clearDisplay();
  drawPlayAnimation(mouth_animation_active_);
  if (root_overlay_visible_) {
    const bool seq = root_item_ == ROOT_SEQ;
    display_.fillRect(2, 9, 60, 14, BLACK);
    if (seq) display_.fillRect(4, 11, 25, 10, WHITE);
    else display_.drawRect(4, 11, 25, 10, WHITE);
    display_.setTextColor(seq ? BLACK : WHITE, seq ? WHITE : BLACK);
    display_.setCursor(8, 12);
    display_.print("SEQ");
    if (!seq) display_.fillRect(34, 11, 25, 10, WHITE);
    else display_.drawRect(34, 11, 25, 10, WHITE);
    display_.setTextColor(seq ? WHITE : BLACK, seq ? BLACK : WHITE);
    display_.setCursor(35, 12);
    display_.print("BASS");
    // Text background pixels can cover the one-pixel right border.
    if (seq) display_.drawRect(34, 11, 25, 10, WHITE);
  }
  display_.setTextColor(WHITE, BLACK);
  drawTransportStatus();
}

void AcidUI::drawBass() {
  display_.clearDisplay();
  const AcidBassConfig &config = bass_.config();
  char label[24] = {};
  int16_t value = 0;
  int16_t minimum = 0;
  int16_t maximum = 1000;
  switch (bass_item_) {
    case BASS_FREQ:
      value = AcidCVDisplayValue(BASS_FREQ, config.frequency);
      minimum = 20;
      maximum = 2000;
      snprintf(label, sizeof(label), "freq:%dHz", value);
      break;
    case BASS_WAVE:
      value = AcidCVDisplayValue(BASS_WAVE, config.wave);
      minimum = ACID_WAVE_SAW;
      maximum = ACID_WAVE_TRIANGLE;
      snprintf(label, sizeof(label), "wav:%s", kWaveNames[value]);
      break;
    case BASS_CUTOFF:
      value = AcidCVDisplayValue(BASS_CUTOFF, config.cutoff);
      minimum = 20;
      maximum = 10000;
      snprintf(label, sizeof(label), "cut:%dHz", value);
      break;
    case BASS_RESONANCE:
      value = AcidCVDisplayValue(BASS_RESONANCE, config.resonance);
      snprintf(label, sizeof(label), "res:%d", value);
      break;
    case BASS_ENV_MOD:
      value = AcidCVDisplayValue(BASS_ENV_MOD, config.env_mod);
      snprintf(label, sizeof(label), "env:%d", value);
      break;
    case BASS_DECAY:
      value = AcidCVDisplayValue(BASS_DECAY, config.decay_ms);
      minimum = 10;
      maximum = 3000;
      snprintf(label, sizeof(label), "dec:%dms", value);
      break;
    case BASS_ACCENT:
      value = AcidCVDisplayValue(BASS_ACCENT, config.accent);
      snprintf(label, sizeof(label), "acc:%d", value);
      break;
    case BASS_SLIDE:
      value = AcidCVDisplayValue(BASS_SLIDE, config.slide_ms);
      minimum = 1;
      maximum = 200;
      snprintf(label, sizeof(label), "sli:%dms", value);
      break;
    case BASS_DRIVE:
      value = AcidCVDisplayValue(BASS_DRIVE, config.drive);
      snprintf(label, sizeof(label), "drv:%d", value);
      break;
    case BASS_LEVEL:
      value = AcidCVDisplayValue(BASS_LEVEL, config.level);
      snprintf(label, sizeof(label), "lvl:%d", value);
      break;
    case BASS_BACK:
      display_.drawLine(35, 8, 28, 12, WHITE);
      display_.drawLine(28, 12, 35, 16, WHITE);
      drawBottomLabel("back");
      drawNavArrows();
      break;
    default: break;
  }
  if (bass_item_ != BASS_BACK) {
    drawKnob(value, minimum, maximum);
    drawBottomLabel(label, editing_bass_);
  }
  drawPageIndex(bass_item_, BASS_ITEM_COUNT);
  if (bass_item_ < BASS_BACK) AcidCVDrawIndicator(bass_item_);
}

void AcidUI::drawSeq() {
  display_.clearDisplay();
  const AcidSequenceConfig &config = sequencer_.config();
  char label[20] = {};
  switch (seq_item_) {
    case SEQ_BACK:
      display_.drawLine(35, 8, 28, 12, WHITE);
      display_.drawLine(28, 12, 35, 16, WHITE);
      drawBottomLabel("back");
      drawNavArrows();
      break;
    case SEQ_CLOCK:
      drawKnob(config.clock, ACID_CLOCK_INTERNAL, ACID_CLOCK_CV2);
      snprintf(label, sizeof(label), "clk:%s", kClockNames[config.clock]);
      drawBottomLabel(label, editing_seq_);
      break;
    case SEQ_TIMING:
      if (config.clock == ACID_CLOCK_INTERNAL) {
        drawKnob(config.bpm, 30, 300);
        snprintf(label, sizeof(label), "bpm:%u", config.bpm);
      } else {
        drawKnob(config.ratio, ACID_RATIO_DIV2, ACID_RATIO_X4);
        snprintf(label, sizeof(label), "ratio:%s", kRatioNames[config.ratio]);
      }
      drawBottomLabel(label, editing_seq_);
      break;
    case SEQ_LENGTH:
      drawKnob(config.length, 1, AcidSequencer::kMaxSteps);
      snprintf(label, sizeof(label), "len:%u", config.length);
      drawBottomLabel(label, editing_seq_);
      break;
    case SEQ_EDIT_STEPS:
      drawKnobLabel("edit", "step");
      drawNavArrows();
      break;
    case SEQ_TRANSPOSE:
      drawKnob(config.transpose, -24, 24);
      snprintf(label, sizeof(label), "trans:%+d", config.transpose);
      drawBottomLabel(label, editing_seq_);
      break;
    default: break;
  }
  drawPageIndex(seq_item_, SEQ_ITEM_COUNT);
  drawTransportStatus();
}

void AcidUI::drawTinyNote(int16_t x, int16_t y, int8_t note) {
  const uint8_t pitch = note % 12;
  const uint8_t octave = 1u + note / 12;
  char label[4];
  snprintf(label, sizeof(label), "%s%u", kPitchNames[pitch], octave);
  int16_t x1, y1;
  uint16_t width, height;
  display_.setFont(&Picopixel);
  display_.getTextBounds(label, 0, 0, &x1, &y1, &width, &height);
  display_.setTextColor(WHITE, BLACK);
  display_.setCursor(x + max(0, (14 - (int16_t)width) / 2), y + 6);
  display_.print(label);
  display_.setFont(NULL);
}

void AcidUI::drawStepCell(uint8_t index, int16_t x) {
  const AcidStep &step = sequencer_.step(index);
  const bool accent = step.flags & ACID_STEP_ACCENT;
  const bool slide = step.flags & ACID_STEP_SLIDE;
  const bool selected = selected_step_ == (int8_t)index;
  constexpr int16_t kNoteY = 9;
  constexpr int16_t kFrameY = 18;
  constexpr int16_t kStateY = 20;
  if (step.note == AcidSequencer::kMutedNote) {
    display_.fillRect(x + 7, kStateY + 3, 2, 2, WHITE);
  } else if (slide) {
    drawTinyNote(x + 1, kNoteY, step.note);
    if (accent) display_.fillRoundRect(x + 4, kStateY, 8, 8, 4, WHITE);
    else display_.drawRoundRect(x + 4, kStateY, 8, 8, 4, WHITE);
  } else {
    drawTinyNote(x + 1, kNoteY, step.note);
    if (accent) display_.fillRect(x + 4, kStateY, 8, 8, WHITE);
    else display_.drawRect(x + 4, kStateY, 8, 8, WHITE);
  }

  if (selected && editing_step_) {
    display_.drawRect(x + 2, kFrameY, 12, 12, WHITE);
  } else if (selected) {
    for (uint8_t offset = 0; offset < 12u; offset += 2u) {
      display_.drawPixel(x + 2 + offset, kFrameY, WHITE);
      display_.drawPixel(x + 2 + offset, kFrameY + 11, WHITE);
      display_.drawPixel(x + 2, kFrameY + offset, WHITE);
      display_.drawPixel(x + 13, kFrameY + offset, WHITE);
    }
  }
  if (sequencer_.activeStep() == (int8_t)index)
    display_.drawLine(x + 4, 31, x + 11, 31, WHITE);
}

void AcidUI::drawSteps() {
  display_.clearDisplay();
  const uint8_t length = sequencer_.config().length;
  const uint8_t grid_page = selected_step_ < 0 ? 0u : selected_step_ / 4u;
  const uint8_t grid_page_count = (length + 3u) / 4u;
  const uint8_t display_page = selected_step_ < 0 ? grid_page_count : grid_page;
  drawPageIndex(display_page, grid_page_count + 1u);
  drawTransportStatus();
  if (selected_step_ < 0) {
    display_.drawLine(35, 8, 28, 12, WHITE);
    display_.drawLine(28, 12, 35, 16, WHITE);
    drawBottomLabel("back");
    drawNavArrows();
    return;
  }
  const uint8_t first = grid_page * 4u;
  const uint8_t last = min((uint8_t)(first + 4u), length);
  for (uint8_t i = first; i < last; ++i) {
    const uint8_t local = i - first;
    const int16_t x = local * 16;
    drawStepCell(i, x);
  }
}

void AcidUI::draw() {
  if (display_asleep_) return;
  switch (page_) {
    case ROOT: drawRoot(); break;
    case SEQ: drawSeq(); break;
    case STEPS: drawSteps(); break;
    case BASS: drawBass(); break;
  }
  display_.setTextColor(WHITE, BLACK);
  display_.setFont(NULL);
  // Multiple UI state changes may redraw in one service iteration. Transfer
  // the final frame only once; CV overlays retain their own display service.
  display_dirty_ = true;
}
