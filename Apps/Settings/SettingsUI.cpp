#include "SettingsUI.h"
#include <stdio.h>
#include <PicoSystemRuntime.h>
#include "../../Fonts/Picopixel.h"

void SettingsUI::begin(bool displayOK, bool down, uint32_t now) {
  displayOK_ = displayOK;
  buttonDown_ = down;
  button_.begin(down, now);
  // setup() already loaded BootConfig into the shared runtime cache.
  settings_.audio_routing = PicoProMonoInputEnabled();
  settings_.screen_rotation = PicoProScreenReversed();
  if (displayOK_) {
    display_.invertDisplay(false);
    PicoProApplyDisplayRotation(display_);
  }
}

void SettingsUI::service(int16_t rotation, bool down, uint32_t now) {
  if (down != buttonDown_) {
    buttonDown_ = down;
    if (navigation_.level == SettingsNavigation::ROOT && !navigation_.editing) dirty_ = true;
  }
  if (rotation) {
    navigation_.rotate(rotation);
    saveError_ = false;
    if (navigation_.level == SettingsNavigation::PROCESS) calibration_.ready();
    dirty_ = true;
  }
  if (button_.service(down, rotation != 0, now)) {
    const auto action = navigation_.click();
    if (action == SettingsNavigation::EDIT_SETTING) {
      navigation_.value = navigation_.page == 0 ? settings_.audio_routing : settings_.screen_rotation;
      saveError_ = false;
    } else if (action == SettingsNavigation::COMMIT_SETTING) {
      PicoBootSystemSettings next = settings_;
      if (navigation_.page == 0) next.audio_routing = navigation_.value;
      else next.screen_rotation = navigation_.value;
      saveError_ = !PicoBootSaveSystemSettings(&next);
      if (!saveError_) {
        settings_ = next;
        PicoProApplySystemSettings(settings_);
        if (displayOK_) PicoProApplyDisplayRotation(display_);
      }
    } else if (action == SettingsNavigation::BEGIN_CALIBRATION) calibration_.begin();
    else if (action == SettingsNavigation::CAPTURE) {
      calibration_.capture(navigation_.page);
      ++navigation_.page;
    } else if (action == SettingsNavigation::SAVE) calibration_.save();
    dirty_ = true;
  }
  const bool liveADC = navigation_.level == SettingsNavigation::PROCESS && navigation_.page < 8;
  if (dirty_ || (liveADC && now - lastDraw_ >= 120)) draw(now);
}

void SettingsUI::centered(const char *text, int16_t y) {
  const uint8_t length = strlen(text);
  // Retain the default font and full eleven-character status/menu wording.
  const uint8_t pitch = length > 10 ? 5 : 6;
  const int16_t x = (64 - length * pitch) / 2;
  display_.setCursor(x, y);
  if (pitch == 6) { display_.print(text); return; }
  // Only eleven-character labels need tighter spacing on the 64-pixel OLED.
  for (uint8_t i = 0; i < length; ++i) {
    display_.setCursor(x + i * pitch, y);
    display_.write((uint8_t)text[i]);
  }
}

void SettingsUI::drawPageIndex() {
  const char label[] = {char('1' + navigation_.page), '/', '3', '\0'};
  int16_t x, y;
  uint16_t width, height;
  display_.setFont(&Picopixel);
  display_.getTextBounds(label, 0, 0, &x, &y, &width, &height);
  display_.drawRect(0, 0, width + 4, 9, WHITE);
  display_.setCursor(2, 6);
  display_.print(label);
  display_.setFont(nullptr);
}

void SettingsUI::drawNavigationArrows() {
  display_.drawLine(7, 13, 4, 16, WHITE);
  display_.drawLine(4, 16, 7, 19, WHITE);
  display_.drawLine(56, 13, 59, 16, WHITE);
  display_.drawLine(59, 16, 56, 19, WHITE);
}

void SettingsUI::drawCalibration() {
  // Layout transplanted from Calibration at c71433cbc1728e870bfb4e19a0ccf477c76e4fc5.
  // No navigation decorations or alternate font inside the process.
  char line[16];
  if (navigation_.page == 8) {
    centered("save", 0);
    snprintf(line, sizeof(line), "c1 %d", (int)calibration_.countsPerVolt(0));
    centered(line, 10);
    snprintf(line, sizeof(line), "c2 %d", (int)calibration_.countsPerVolt(1));
    centered(line, 18);
  } else {
    snprintf(line, sizeof(line), "cv%d %dv", navigation_.page / 4 + 1, navigation_.page % 4);
    centered(line, 0);
    snprintf(line, sizeof(line), "adc %u", calibration_.raw(navigation_.page));
    centered(line, 10);
    centered("press set", 18);
  }
  centered(calibration_.status(), 25);
}

void SettingsUI::draw(uint32_t now) {
  dirty_ = false; lastDraw_ = now;
  if (!displayOK_) return;
  display_.clearDisplay();
  display_.setFont(nullptr); display_.setTextSize(1); display_.setTextColor(WHITE, BLACK);
  if (navigation_.level == SettingsNavigation::PROCESS) {
    drawCalibration();
  } else if (navigation_.level == SettingsNavigation::CONFIRM_CALIBRATION) {
    centered("Are you", 0); centered("sure?", 9);
    drawNavigationArrows();
    display_.fillRect(0, 23, 64, 9, WHITE); display_.setTextColor(BLACK, WHITE);
    centered(navigation_.page == 0 ? "no" : "yes", 24);
  } else {
    const uint8_t value = navigation_.editing ? navigation_.value :
      (navigation_.page == 0 ? settings_.audio_routing : settings_.screen_rotation);
    const char *valueText = navigation_.page == 0 ? (value ? "mono*2" : "stereo") :
      navigation_.page == 1 ? (value ? "reverse" : "normal") : "process";
    centered(saveError_ ? "save err" : valueText, 10);
    drawNavigationArrows();
    drawPageIndex();
    if (navigation_.editing || buttonDown_) {
      display_.fillRect(0, 23, 64, 9, WHITE); display_.setTextColor(BLACK, WHITE);
    }
    static const char *const labels[] = {"routing", "rotation", "calibration"};
    centered(labels[navigation_.page], 24);
  }
  display_.display();
}
