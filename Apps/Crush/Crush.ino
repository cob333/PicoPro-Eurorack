// Stereo Bitcrush for PicoPro, adapted from the PicoFX Bitcrush sketch.

#include "PicoPro.h"
#include <I2S.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ClickEncoder.h"
#include "PicoAppState.h"

#define SAMPLERATE 44100
#define DEBOUNCE_CYCLES 100

enum UISTATES { RUN, DORMANT, WAIT_BUTTON_RELEASE };
int16_t UI_state = RUN;

I2S i2s(INPUT_PULLUP);
ClickEncoder menuenc(ENCA_IN, ENCB_IN, ENCSW_IN, ENCDIVIDE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
int32_t displaytimer;

#include "ui/CVModulation.h"

int16_t crush_bits = 12;
int16_t crush_downsample = 4;
int16_t crush_mix = 500;
int16_t crush_level = 900;

struct CrushControls {
  uint8_t bits;
  uint8_t downsample;
  uint16_t mix_q16;
  uint16_t level_q16;
};

static volatile uint32_t crush_control_revision = 0;
static volatile CrushControls crush_controls = {12, 4, 32768, 58982};
static volatile int16_t crush_exit_gain = 1000;
static volatile bool crush_audio_ready = false;

#define CRUSH_STATE_TAG PICOPRO_APP_STATE_TAG('C', 'R', 'S', '1')
struct CrushState {
  int16_t bits;
  int16_t downsample;
  int16_t mix;
  int16_t level;
  PicoCVPersistentState cv;
};

static void sanitizeCrushState() {
  crush_bits = constrain(crush_bits, 1, 16);
  crush_downsample = constrain(crush_downsample, 1, 20);
  crush_mix = constrain(crush_mix, 0, 1000);
  crush_level = constrain(crush_level, 0, 1000);
}

static void loadCrushState() {
  CrushState state;
  if (!PicoAppStateLoad(CRUSH_STATE_TAG, &state, sizeof(state))) return;
  crush_bits = state.bits;
  crush_downsample = state.downsample;
  crush_mix = state.mix;
  crush_level = state.level;
  PicoCVImportState(&state.cv);
  sanitizeCrushState();
}

static void saveCrushState() {
  sanitizeCrushState();
  CrushState state = {crush_bits, crush_downsample, crush_mix, crush_level};
  PicoCVExportState(&state.cv);
  PicoAppStateSave(CRUSH_STATE_TAG, &state, sizeof(state));
}

void updateCrushMenu(int16_t, int16_t) { sanitizeCrushState(); }
#include "Crush_Menu.h"
static void drawCrushMenuItem(const menu *item, bool editing,
                              int8_t nav_dir, uint8_t index, uint8_t total);
#define PICOPRO_MENU_DRAW_ITEM(item, editing, nav_dir, index, total) \
  drawCrushMenuItem((item), (editing), (nav_dir), (index), (total))
#include "MenuSystem.h"
#undef PICOPRO_MENU_DRAW_ITEM

static void drawCrushPixelKnob(uint8_t bits) {
  constexpr int16_t x0 = 22;
  constexpr int16_t y0 = 1;
  constexpr uint8_t width = 21;
  constexpr uint8_t height = 20;
  constexpr uint16_t pixel_count = width * height;
  uint8_t source[(pixel_count + 7u) / 8u] = {};

  // Capture the standard knob from the framebuffer, then enlarge groups of
  // its pixels into coarse square cells.  The transform is confined to the
  // center, so page, CV, navigation and parameter text remain untouched.
  for (uint8_t y = 0; y < height; ++y) {
    for (uint8_t x = 0; x < width; ++x) {
      if (!display.getPixel(x0 + x, y0 + y)) continue;
      const uint16_t bit = static_cast<uint16_t>(y) * width + x;
      source[bit >> 3] |= 1u << (bit & 7u);
    }
  }
  display.fillRect(x0, y0, width, height, BLACK);

  const uint8_t block = 1u + ((16u - bits + 3u) / 4u);
  for (uint8_t cell_y = 0; cell_y < height; cell_y += block) {
    for (uint8_t cell_x = 0; cell_x < width; cell_x += block) {
      bool lit = false;
      const uint8_t cell_h = min(block, static_cast<uint8_t>(height - cell_y));
      const uint8_t cell_w = min(block, static_cast<uint8_t>(width - cell_x));
      for (uint8_t y = 0; y < cell_h && !lit; ++y) {
        for (uint8_t x = 0; x < cell_w; ++x) {
          const uint16_t bit = static_cast<uint16_t>(cell_y + y) * width +
                               cell_x + x;
          if (source[bit >> 3] & (1u << (bit & 7u))) {
            lit = true;
            break;
          }
        }
      }
      if (lit) display.fillRect(x0 + cell_x, y0 + cell_y,
                                cell_w, cell_h, WHITE);
    }
  }
}

static void drawCrushMenuItem(const menu *item, bool editing,
                              int8_t nav_dir, uint8_t index, uint8_t total) {
  PicoKnobDrawMenuItem(item, editing, nav_dir, index, total);
  if (item->parameter != &crush_bits) return;
  const int16_t displayed_bits = constrain(
      PicoCVDisplayValue(index, crush_bits), item->min, item->max);
  if (displayed_bits >= 16) return;
  drawCrushPixelKnob(static_cast<uint8_t>(displayed_bits));
}

static void alarm_in_us_arm(uint32_t delay_us);
static void alarm_irq() {
  menuenc.service();
  hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
  alarm_in_us_arm(TIMER_MICROS);
}
static void alarm_in_us_arm(uint32_t delay_us) {
  timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + delay_us;
}
static void alarm_in_us(uint32_t delay_us) {
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
  irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
  irq_set_enabled(ALARM_IRQ, true);
  alarm_in_us_arm(delay_us);
}

static uint16_t crushToQ16(int16_t value) {
  return static_cast<uint16_t>((static_cast<uint32_t>(value) * 65535u) / 1000u);
}

static void serviceCrushCV() {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if ((now - last_ms) < 5) return;
  last_ms = now;

  CrushControls next;
  next.bits = PicoCVModulatedValue(0, crush_bits, menus[0].min, menus[0].max);
  next.downsample = PicoCVModulatedValue(1, crush_downsample,
                                         menus[1].min, menus[1].max);
  next.mix_q16 = crushToQ16(PicoCVModulatedValue(
      2, crush_mix, menus[2].min, menus[2].max));
  next.level_q16 = crushToQ16(PicoCVModulatedValue(
      3, crush_level, menus[3].min, menus[3].max));

  ++crush_control_revision;
  crush_controls.bits = next.bits;
  crush_controls.downsample = next.downsample;
  crush_controls.mix_q16 = next.mix_q16;
  crush_controls.level_q16 = next.level_q16;
  ++crush_control_revision;
}

static void prepareCrushExit() {
  crush_exit_gain = 0;
  delay(32);
}

static int32_t crushSample(int32_t sample, uint8_t bits) {
  int32_t sample16 = sample >> 16;
  const uint8_t shift = 16u - bits;
  if (shift) sample16 = (sample16 >> shift) * (1L << shift);
  return sample16 * 65536;
}

static CrushControls readCrushControls() {
  CrushControls snapshot;
  while (true) {
    const uint32_t before = crush_control_revision;
    if (before & 1u) continue;
    snapshot.bits = crush_controls.bits;
    snapshot.downsample = crush_controls.downsample;
    snapshot.mix_q16 = crush_controls.mix_q16;
    snapshot.level_q16 = crush_controls.level_q16;
    const uint32_t after = crush_control_revision;
    if (before == after && !(after & 1u)) return snapshot;
  }
}

void setup() {
  pinMode(ENCA_IN, INPUT_PULLUP);
  pinMode(ENCB_IN, INPUT_PULLUP);
  pinMode(ENCSW_IN, INPUT_PULLUP);
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();
  alarm_in_us(TIMER_MICROS);
  analogReadResolution(AD_BITS);

  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK);
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(SAMPLERATE);
  i2s.begin();

  PicoCVBindMenus(menus, NUM_MENUS);
  loadCrushState();
  serviceCrushCV();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) while (true) {}
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  displaytimer = millis();
  drawmenu(0);
  menuenc.getValue();
  crush_audio_ready = true;
}

void loop() {
  static int16_t debounce_counter = 0;
  PicoProServiceSelectorExit(ENCSW_IN, menuenc, saveCrushState, prepareCrushExit);
  serviceCrushCV();
  switch (UI_state) {
    case RUN: domenus(); break;
    case DORMANT:
      if (menuenc.getValue() || !digitalRead(ENCSW_IN)) {
        UI_state = WAIT_BUTTON_RELEASE;
        debounce_counter = DEBOUNCE_CYCLES;
        drawmenu(menuindex);
      }
      break;
    case WAIT_BUTTON_RELEASE:
      if (digitalRead(ENCSW_IN) && --debounce_counter <= 0) UI_state = RUN;
      break;
  }
  if ((millis() - displaytimer) > DISPLAY_BLANK_MS && UI_state == RUN) {
    UI_state = DORMANT;
    blankdisplay();
  }
}

void setup1() {
  while (!crush_audio_ready) tight_loop_contents();
}

void loop1() {
  static CrushControls controls = {12, 4, 32768, 58982};
  static uint8_t control_divider = 0;
  static uint8_t downsample_counter = 0;
  static int32_t held_left = 0;
  static int32_t held_right = 0;
  static int32_t smooth_mix_q16 = 32768;
  static int32_t smooth_level_q16 = 58982;
  static int16_t exit_gain = 1000;

  if (++control_divider >= 32) {
    control_divider = 0;
    controls = readCrushControls();
    if (downsample_counter >= controls.downsample) downsample_counter = 0;
  }

  const int32_t dry_left = i2s.read();
  const int32_t dry_right = i2s.read();
  if (downsample_counter == 0) {
    held_left = crushSample(dry_left, controls.bits);
    held_right = crushSample(dry_right, controls.bits);
    downsample_counter = controls.downsample;
  }
  --downsample_counter;

  smooth_mix_q16 += (static_cast<int32_t>(controls.mix_q16) - smooth_mix_q16) >> 4;
  smooth_level_q16 +=
      (static_cast<int32_t>(controls.level_q16) - smooth_level_q16) >> 4;
  if (exit_gain > crush_exit_gain) --exit_gain;
  const int32_t output_level =
      (smooth_level_q16 * static_cast<int32_t>(exit_gain)) / 1000;

  const int64_t wet_delta_left = static_cast<int64_t>(held_left) - dry_left;
  const int64_t wet_delta_right = static_cast<int64_t>(held_right) - dry_right;
  const int32_t mixed_left = dry_left + ((wet_delta_left * smooth_mix_q16) >> 16);
  const int32_t mixed_right = dry_right + ((wet_delta_right * smooth_mix_q16) >> 16);
  i2s.write(static_cast<int32_t>((static_cast<int64_t>(mixed_left) * output_level) >> 16));
  i2s.write(static_cast<int32_t>((static_cast<int64_t>(mixed_right) * output_level) >> 16));
}
