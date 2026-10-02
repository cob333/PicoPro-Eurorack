#ifndef PICOPRO_STANDBY_DISPLAY_H_
#define PICOPRO_STANDBY_DISPLAY_H_

#include <stdint.h>
#include "../OutputMeter.h"

// UI-only component; independent of application menus and the audio backend.
class PicoStandbyDisplay {
 public:
  enum Result : uint8_t { Normal, Meter, Restore, WakeWait };
  static constexpr uint32_t kIdleMs = 30000u;
  static constexpr uint32_t kFrameMs = 40u;
  static constexpr uint32_t kWakeReleaseMs = 280u; // Drain delayed encoder clicks.

  Result poll(uint32_t now, bool activity, bool buttonDown, bool buttonEvent = false) {
    activity = activity || buttonDown || buttonEvent;
    if (!initialized_) { initialized_ = true; lastActivity_ = now; }
    if (state_ == Meter) {
      if (!activity) return Meter;
      lastActivity_ = releasedMs_ = now;
      // A short press may already have ended before the UI sees its event.
      // Still drain its delayed click/double-click, not just the raw release.
      state_ = (buttonDown || buttonEvent) ? WakeWait : Normal;
      return Restore; // Consume the wake operation, including its rotation.
    }
    if (state_ == WakeWait) {
      lastActivity_ = now;
      if (buttonDown) releasedMs_ = now;
      if (uint32_t(now - releasedMs_) >= kWakeReleaseMs) state_ = Normal;
      return WakeWait;
    }
    if (activity) lastActivity_ = now;
    if (uint32_t(now - lastActivity_) >= kIdleMs) {
      state_ = Meter;
      frameValid_ = false;
      return Meter;
    }
    return Normal;
  }

  bool active() const { return state_ != Normal; }

  static uint8_t segmentCount(uint16_t level) {
    // -48, -42, -36, -30, -24, -18, -12, -9, -6, -3, -1, 0 dBFS.
    // Fixed thresholds avoid UI-side logarithms. One LSB of full-scale
    // tolerance also recognizes the positive maximum of 16-bit output PCM.
    static constexpr uint16_t thresholds[12] = {
      261, 521, 1039, 2073, 4135, 8251,
      16462, 23253, 32846, 46396, 58409, 65534
    };
    uint8_t count = 0;
    while (count < 12 && level >= thresholds[count]) ++count;
    return count;
  }

  template <class Display>
  void draw(Display &display, uint32_t now) {
    if (frameValid_ && uint32_t(now - drawnMs_) < kFrameMs) return;
    const PicoOutputLevels levels = PicoOutputMeterRead();
    const uint8_t left = segmentCount(levels.left), right = segmentCount(levels.right);
    drawnMs_ = now;
    if (frameValid_ && left == lastLeft_ && right == lastRight_) return;
    frameValid_ = true;
    lastLeft_ = left; lastRight_ = right;
    display.clearDisplay();
    drawRow(display, left, 4);
    drawRow(display, right, 18);
    display.display();
  }

 private:
  template <class Display>
  static void drawRow(Display &display, uint8_t count, uint8_t y) {
    // Twelve 4x10 cells with a one-pixel horizontal gap. The two rows have
    // four-pixel top/bottom margins and a four-pixel vertical gap.
    // Only active cells are drawn; the final two are solid warning cells.
    for (uint8_t i = 0; i < count; ++i) {
      const uint8_t x = 2 + i * 5;
      if (i >= 10) display.fillRect(x, y, 4, 10, 1);
      else display.drawRect(x, y, 4, 10, 1);
    }
  }

  Result state_ = Normal;
  bool initialized_ = false, frameValid_ = false;
  uint8_t lastLeft_ = 0, lastRight_ = 0;
  uint32_t lastActivity_ = 0, releasedMs_ = 0, drawnMs_ = 0;
};

#endif
