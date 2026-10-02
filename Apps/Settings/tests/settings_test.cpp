#include "../SettingsNavigation.h"
#include "../SettingsCalibration.h"
#include <cassert>
#include <cstdio>

static PicoBootCalibration stored = {580, 590, 5456, 3250, 3260, 123, {7, 8}};
static uint16_t samples[2] = {};
static unsigned saves = 0, reads = 0, delays = 0;
static bool saveOK = true;
int PicoBootLoadCalibration(PicoBootCalibration *out) { *out = stored; return 1; }
int PicoBootCVCalibrationValid(const PicoBootCalibration *c) {
  return std::isfinite(c->cv1_counts_per_volt) && std::isfinite(c->cv2_counts_per_volt) &&
    c->cv1_counts_per_volt >= 100 && c->cv1_counts_per_volt <= 1200 &&
    c->cv2_counts_per_volt >= 100 && c->cv2_counts_per_volt <= 1200 &&
    c->cv1_zero_counts >= 0 && c->cv1_zero_counts <= 4095 &&
    c->cv2_zero_counts >= 0 && c->cv2_zero_counts <= 4095;
}
int PicoBootSaveCalibration(const PicoBootCalibration *c) {
  if (!saveOK) return 0;
  stored = *c; ++saves; return 1;
}
uint16_t PicoCVInputReadRawFresh(uint8_t cv) { ++reads; return samples[cv]; }
void delay(uint32_t ms) { assert(ms == 1); ++delays; }

static void capture(SettingsCalibration &c, const uint16_t values[4]) {
  for (uint8_t cv = 0; cv < 2; ++cv) {
    for (uint8_t v = 0; v < 4; ++v) { samples[cv] = values[v]; c.capture(cv * 4 + v); }
  }
}

int main() {
  SettingsNavigation n;
  assert(n.count() == 3);
  assert(n.click() == n.EDIT_SETTING && n.editing && n.page == 0);
  n.rotate(1); assert(n.value == 1 && n.page == 0);
  assert(n.click() == n.COMMIT_SETTING && !n.editing);
  n.rotate(1); assert(n.click() == n.EDIT_SETTING && n.page == 1);
  assert(n.click() == n.COMMIT_SETTING && !n.editing);
  n.rotate(1); assert(n.click() == n.NONE && n.level == n.CONFIRM_CALIBRATION && n.page == 0);
  assert(n.click() == n.NONE && n.level == n.ROOT && n.page == 2); // NO cancels safely.
  assert(n.click() == n.NONE && n.level == n.CONFIRM_CALIBRATION && n.page == 0);
  n.rotate(1); assert(n.click() == n.BEGIN_CALIBRATION && n.level == n.PROCESS && n.page == 0);
  assert(n.count() == 9);
  n.rotate(-1); assert(n.page == 8 && n.click() == n.SAVE);
  assert(n.level == n.PROCESS && n.page == 8); // Save stays on the save page.
  n.rotate(1); assert(n.page == 0 && n.click() == n.CAPTURE);
  n.rotate(-100); assert(n.page < n.count());
  n.level = n.PROCESS; n.page = 8; assert(n.click() == n.SAVE);

  SettingsButton b;
  b.begin(true, 0); // Held on entry must not trigger.
  assert(!b.service(false, false, 5)); assert(!b.service(false, false, 25));
  assert(!b.service(true, false, 30)); assert(!b.service(true, false, 50));
  assert(!b.service(false, false, 60)); assert(b.service(false, false, 80));
  assert(!b.service(false, false, 100)); // Release cannot leak into next level.
  assert(!b.service(true, false, 110)); assert(!b.service(true, false, 130));
  assert(!b.service(true, true, 140));
  assert(!b.service(false, false, 150)); assert(!b.service(false, false, 170));
  assert(!b.service(true, false, 180)); assert(!b.service(true, false, 200));
  assert(!b.service(false, false, 2100)); assert(!b.service(false, false, 2120));

  SettingsCalibration c; c.begin();
  assert(!c.save() && saves == 0 && !strcmp(c.status(), "need all"));
  const uint16_t valid[] = {3250, 2670, 2090, 1510};
  capture(c, valid);
  assert(!strcmp(c.status(), "set c2 3"));
  c.ready(); assert(!strcmp(c.status(), "ready"));
  assert(reads == 256 && delays == 256);
  assert(c.save() && saves == 1);
  assert(!strcmp(c.status(), "saved"));
  assert(fabsf(stored.cv1_counts_per_volt - 580) < .01f);
  assert(stored.cvout_counts_per_volt == 5456 && stored.cvout_zero_counts == 123);
  assert(stored.reserved[0] == 7 && stored.reserved[1] == 8);
  c.begin(); assert(!c.save() && saves == 1); // Restart resets completion.
  const uint16_t unordered[] = {3250, 2670, 2800, 1510};
  capture(c, unordered); assert(!c.save() && saves == 1);
  c.begin(); const uint16_t residual[] = {3250, 2820, 2090, 1510};
  capture(c, residual); assert(!c.save() && saves == 1);
  c.begin(); const uint16_t flat[] = {3250, 3240, 3230, 3220};
  capture(c, flat); assert(!c.save() && saves == 1);
  c.begin(); capture(c, valid); saveOK = false;
  assert(!c.save() && saves == 1 && !strcmp(c.status(), "save err"));
  for (uint32_t flags = 0; flags < 65536; ++flags) {
    for (uint8_t routing = 0; routing < 2; ++routing) for (uint8_t rotation = 0; rotation < 2; ++rotation) {
      const PicoBootSystemSettings preferences = {routing, rotation};
      assert(PicoBootSystemSettingsValid(&preferences));
      const uint16_t packed = PicoBootSystemSettingsFlags(flags, &preferences);
      assert((packed & ~3u) == (flags & ~3u));
      const auto decoded = PicoBootSystemSettingsFromFlags(packed);
      assert(decoded.audio_routing == routing && decoded.screen_rotation == rotation);
    }
  }
  const PicoBootSystemSettings invalid = {2, 0};
  assert(!PicoBootSystemSettingsValid(&invalid) && !PicoBootSystemSettingsValid(nullptr));
  n.level = n.ROOT; n.editing = true;
  for (int32_t delta = -32768; delta <= 32767; ++delta) {
    n.value = 0; n.rotate((int16_t)delta);
    assert(n.value == (delta % 2 != 0));
  }
  c.begin();
  for (uint8_t step = 0; step < 7; ++step) {
    samples[step / 4] = valid[step % 4]; c.capture(step);
  }
  c.capture(6); // Repeated captures cannot stand in for a missing point.
  assert(!c.save() && !strcmp(c.status(), "need all"));
  samples[1] = valid[3]; c.capture(7); saveOK = true;
  assert(c.save() && saves == 2);
  puts("Settings navigation, release guards and calibration regression passed");
}
