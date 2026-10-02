#ifndef PICOPRO_SETTINGS_NAVIGATION_H
#define PICOPRO_SETTINGS_NAVIGATION_H
#include <stdint.h>

// Pure, bounded page navigation. Hardware/UI and calibration live separately.
struct SettingsNavigation {
  enum Level { ROOT, CONFIRM_CALIBRATION, PROCESS };
  enum Action { NONE, EDIT_SETTING, COMMIT_SETTING, BEGIN_CALIBRATION, CAPTURE, SAVE };
  Level level = ROOT;
  uint8_t page = 0;
  bool editing = false;
  uint8_t value = 0;
  uint8_t count() const { return level == ROOT ? 3 : level == PROCESS ? 9 : 2; }
  void rotate(int16_t delta) {
    if (level == ROOT && editing) {
      if (delta % 2) value ^= 1;
      return;
    }
    const uint8_t pages = count();
    int16_t next = (page + delta % pages) % pages;
    page = next < 0 ? next + pages : next;
  }
  Action click() {
    if (level == ROOT) {
      if (page < 2) {
        editing = !editing;
        return editing ? EDIT_SETTING : COMMIT_SETTING;
      }
      level = CONFIRM_CALIBRATION; page = 0; // NO is always the safe default.
    } else if (level == CONFIRM_CALIBRATION) {
      if (page == 0) { level = ROOT; page = 2; }
      else { level = PROCESS; page = 0; return BEGIN_CALIBRATION; }
    } else {
      if (page < 8) return CAPTURE;
      return SAVE;
    }
    return NONE;
  }
};

// Debounced short-release event: no delayed multiclick and no transition leak.
class SettingsButton {
 public:
  void begin(bool down, uint32_t now) {
    raw_ = stable_ = down; changed_ = now; armed_ = false;
  }
  bool service(bool down, bool rotated, uint32_t now) {
    if (down != raw_) { raw_ = down; changed_ = now; }
    if (rotated) armed_ = false;
    if (raw_ == stable_ || now - changed_ < 20) return false;
    stable_ = raw_;
    if (stable_) { armed_ = !rotated; pressed_ = now; return false; }
    const bool click = armed_ && now - pressed_ < 1800;
    armed_ = false;
    return click;
  }
 private:
  bool raw_ = false, stable_ = false, armed_ = false;
  uint32_t changed_ = 0, pressed_ = 0;
};
#endif
