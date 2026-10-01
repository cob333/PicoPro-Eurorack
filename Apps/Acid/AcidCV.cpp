#include "AcidCV.h"

#include <Adafruit_SSD1306.h>

#include "CVInput.h"
#include "MenuTypes.h"
#include "PicoBootConfig.h"

extern Adafruit_SSD1306 display;
static int32_t displaytimer = 0;

#include "ui/CVModulation.h"
#include "ui/KnobDisplay.h"

namespace {

int16_t values[ACID_CV_PARAMETER_COUNT] = {};
const char *wave_names[] = {"saw", "square", "tri"};

menu menus[ACID_CV_PARAMETER_COUNT] = {
    {"freq", 20, 2000, 1, TYPE_INTEGER, nullptr, &values[0], 0, nullptr},
    {"wav", 0, 2, 1, TYPE_TEXT, wave_names, &values[1], 0, nullptr},
    {"cutoff", 20, 10000, 10, TYPE_INTEGER, nullptr, &values[2], 0, nullptr},
    {"res", 0, 1000, 10, TYPE_INTEGER, nullptr, &values[3], 0, nullptr},
    {"env", 0, 1000, 10, TYPE_INTEGER, nullptr, &values[4], 0, nullptr},
    {"dec", 10, 3000, 10, TYPE_INTEGER, nullptr, &values[5], 0, nullptr},
    {"acc", 0, 1000, 10, TYPE_INTEGER, nullptr, &values[6], 0, nullptr},
    {"sli", 1, 200, 1, TYPE_INTEGER, nullptr, &values[7], 0, nullptr},
    {"drv", 0, 1000, 10, TYPE_INTEGER, nullptr, &values[8], 0, nullptr},
    {"lvl", 0, 1000, 10, TYPE_INTEGER, nullptr, &values[9], 0, nullptr},
};

uint32_t last_modulation_ms = 0;

void syncValues(const AcidBassConfig &config) {
  values[0] = config.frequency;
  values[1] = config.wave;
  values[2] = config.cutoff;
  values[3] = config.resonance;
  values[4] = config.env_mod;
  values[5] = config.decay_ms;
  values[6] = config.accent;
  values[7] = config.slide_ms;
  values[8] = config.drive;
  values[9] = config.level;
}

}  // namespace

void AcidCVBegin(const AcidBassConfig &config) {
  syncValues(config);
  PicoCVBindMenus(menus, ACID_CV_PARAMETER_COUNT);
}

void AcidCVServiceModulation(AcidBass &bass) {
  const uint32_t now_ms = millis();
  if ((now_ms - last_modulation_ms) < 5u) return;
  last_modulation_ms = now_ms;
  const AcidBassConfig &base = bass.config();
  syncValues(base);
  AcidBassConfig effective = base;
  const float frequency_hz = PicoCVModulatedFrequencyHz(
      0, (float)base.frequency, 20.0f, 2000.0f);
  effective.wave = PicoCVModulatedValue(1, base.wave, 0, 2);
  effective.cutoff = PicoCVModulatedValue(2, base.cutoff, 20, 10000);
  effective.resonance = PicoCVModulatedValue(3, base.resonance, 0, 1000);
  effective.env_mod = PicoCVModulatedValue(4, base.env_mod, 0, 1000);
  effective.decay_ms = PicoCVModulatedValue(5, base.decay_ms, 10, 3000);
  effective.accent = PicoCVModulatedValue(6, base.accent, 0, 1000);
  effective.slide_ms = PicoCVModulatedValue(7, base.slide_ms, 1, 200);
  effective.drive = PicoCVModulatedValue(8, base.drive, 0, 1000);
  effective.level = PicoCVModulatedValue(9, base.level, 0, 1000);
  bass.setModulatedControls(frequency_hz, effective);
}

uint8_t AcidCVServiceOverlay(int16_t movement, bool button_down,
                             ClickEncoder::Button button) {
  return PicoCVServiceOverlay(menus, ACID_CV_PARAMETER_COUNT,
                              movement, button_down, button);
}

bool AcidCVHandleEntry(ClickEncoder::Button button, uint8_t index) {
  if (index >= ACID_CV_PARAMETER_COUNT ||
      !PicoCVHandleEntryButton(button, index)) {
    return false;
  }
  PicoCVDrawOverlay(&menus[index]);
  return true;
}

void AcidCVDrawIndicator(uint8_t index) {
  if (index >= ACID_CV_PARAMETER_COUNT) return;
  PicoKnobDrawCVIndicator(index);
}

int16_t AcidCVDisplayValue(uint8_t index, int16_t base) {
  return index < ACID_CV_PARAMETER_COUNT
             ? PicoCVDisplayValue(index, base)
             : base;
}

bool AcidCVShouldRefresh(uint8_t index, uint32_t now_ms) {
  return index < ACID_CV_PARAMETER_COUNT &&
         PicoCVShouldRefreshDisplay(index, now_ms);
}

void AcidCVExportState(PicoCVPersistentState *state) {
  PicoCVExportState(state);
}

void AcidCVImportState(const PicoCVPersistentState *state) {
  PicoCVImportState(state);
}
