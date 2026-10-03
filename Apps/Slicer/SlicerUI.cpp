#include "SlicerUI.h"
#include "SlicerControls.h"
#include <PicoPro_io.h>
#include <PicoSystemRuntime.h>
#include <CVInput.h>
#include <BootSelector.h>
#include <PicoAppState.h>
#include <ClickEncoder.h>
#include <Adafruit_SSD1306.h>
#include <MenuTypes.h>
#include <ui/StandbyDisplay.h>

constexpr int64_t kEncoderServiceUs = 1000;
static_assert(-kEncoderServiceUs == -1000, "Encoder timer period must be signed");

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
static ClickEncoder encoder(ENCA_IN, ENCB_IN, ENCSW_IN, ENCDIVIDE);
static repeating_timer_t encoderTimer;
static bool displayOK = false, editing = false, buttonArmed = true;
static uint8_t page = 0;
static uint32_t lastControlMs = 0, lastServiceMs = UINT32_MAX;
static PicoStandbyDisplay standby;

enum Parameter : uint8_t { MODE, PATTERN, CLOCK, BPM, RATIO, ATTACK, DUTY, MIX, DEPTH, COUNT };
constexpr uint8_t kPageCount = COUNT - 1; // BPM and ratio share one visible page.
static int16_t values[COUNT] = {0, 1, 0, 120, 2, 100, 500, 1000, 1000};
static const char *modeNames[] = {"single", "tremo", "harmo"};
static const char *clockNames[] = {"int", "cv1", "cv2"};
static const char *ratioNames[] = {"/4", "/2", "x1", "x2", "x4"};
static menu menus[COUNT] = {
  {"mode", 0, 2, 1, TYPE_TEXT, modeNames, &values[MODE], 0, nullptr},
  {"ptn", 1, 16, 1, TYPE_INTEGER, nullptr, &values[PATTERN], 0, nullptr},
  {"clk", 0, 2, 1, TYPE_TEXT, clockNames, &values[CLOCK], 0, nullptr},
  {"bpm", 30, 300, 1, TYPE_INTEGER, nullptr, &values[BPM], 0, nullptr},
  {"ratio", 0, SlicerEngine::kRatioCount - 1, 1, TYPE_TEXT, ratioNames, &values[RATIO], 0, nullptr},
  {"atk", 0, 1000, 10, TYPE_FLOAT, nullptr, &values[ATTACK], 0, nullptr},
  {"duty", 0, 1000, 10, TYPE_FLOAT, nullptr, &values[DUTY], 0, nullptr},
  {"mix", 0, 1000, 10, TYPE_FLOAT, nullptr, &values[MIX], 0, nullptr},
  {"depth", 0, 1000, 10, TYPE_FLOAT, nullptr, &values[DEPTH], 0, nullptr},
};

// Keep shared menu/CV state in this single translation unit.
#include <ui/KnobDisplay.h>

static uint8_t parameter() { return page < 3 ? page : page == 3 ? (values[CLOCK] ? RATIO : BPM) : page + 1; }
static bool assignable(uint8_t index) { return index == PATTERN || index == BPM || index >= ATTACK; }
static bool serviceEncoder(repeating_timer_t *) { encoder.service(); return true; }

static void publish() {
  SlicerControlPacket packet;
  auto &p = packet.parameters;
  p.mode = static_cast<SlicerMode>(values[MODE]);
  p.pattern = PicoCVModulatedValue(PATTERN, values[PATTERN], 1, 16) - 1;
  p.clock = static_cast<SlicerClock>(values[CLOCK]);
  p.ratio = values[RATIO];
  p.bpm = values[CLOCK] ? values[BPM] : PicoCVModulatedValue(BPM, values[BPM], 30, 300);
  p.attack = PicoCVModulatedValue(ATTACK, values[ATTACK], 0, 1000) * .001f;
  p.duty = PicoCVModulatedValue(DUTY, values[DUTY], 0, 1000) * .001f;
  p.mix = PicoCVModulatedValue(MIX, values[MIX], 0, 1000) * .001f;
  p.depth = PicoCVModulatedValue(DEPTH, values[DEPTH], 0, 1000) * .001f;
  packet.clockEpoch = PicoCVInputClockCaptureEpoch();
  slicerControls.publish(packet);
}

static void draw() {
  if (!displayOK) return;
  const uint8_t index = parameter();
  PicoKnobDrawMenuItem(&menus[index], editing, 0, index);
  PicoKnobDrawIndex(page, kPageCount);
  if (!assignable(index)) display.fillRect(49, 0, 15, 9, BLACK);
  display.display();
}

struct SavedState { int16_t values[COUNT]; PicoCVPersistentState cv; };
static_assert(sizeof(SavedState) <= 232, "Slicer state exceeds shared record payload");
constexpr uint32_t kLegacyStateTag = PICOPRO_APP_STATE_TAG('S','L','C','1');
constexpr uint32_t kStateTag = PICOPRO_APP_STATE_TAG('S','L','C','2');
static void save() {
  SavedState state{};
  memcpy(state.values, values, sizeof(values));
  PicoCVExportState(&state.cv);
  PicoAppStateSave(kStateTag, &state, sizeof(state));
}
static void prepareExit() {
  slicerControls.mute();
  PicoCVInputSelectClockCapture(0);
  delay(20); // Control core only; bounded fade/drain before Flash lockout.
}

void SlicerUI::begin() {
  pinMode(ENCA_IN, INPUT_PULLUP); pinMode(ENCB_IN, INPUT_PULLUP); pinMode(ENCSW_IN, INPUT_PULLUP);
  Wire.setSDA(PIN_WIRE_SDA); Wire.setSCL(PIN_WIRE_SCL); Wire.begin();
  PicoCVInputBegin();
  PicoCVBindMenus(menus, COUNT);
  SavedState state;
  bool loaded = PicoAppStateLoad(kStateTag, &state, sizeof(state));
  if (!loaded && PicoAppStateLoad(kLegacyStateTag, &state, sizeof(state))) {
    // Inserting /4 must preserve the saved /2, x1, x2 or x4 label.
    state.values[RATIO] = constrain(state.values[RATIO], 0, 3) + 1;
    loaded = true;
  }
  if (loaded) {
    for (uint8_t i = 0; i < COUNT; ++i) values[i] = constrain(state.values[i], menus[i].min, menus[i].max);
    PicoCVImportState(&state.cv);
  }
  PicoCVInputSelectClockCapture(values[CLOCK]);
  publish();
  displayOK = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  PicoProApplyDisplayRotation(display);
  display.setTextSize(1); display.setTextColor(WHITE, BLACK);
  buttonArmed = digitalRead(ENCSW_IN);
  add_repeating_timer_us(-kEncoderServiceUs, serviceEncoder, nullptr, &encoderTimer);
  encoder.getValue(); draw();
}

void SlicerUI::service() {
  const uint32_t now = millis();
  // Match the 1 kHz encoder service; do not hammer shared memory between ticks.
  if (now == lastServiceMs) return;
  lastServiceMs = now;
  PicoProServiceSelectorExit(ENCSW_IN, encoder, save, prepareExit);
  if (now - lastControlMs >= 5) { lastControlMs = now; publish(); }
  const int16_t rotation = encoder.getValue();
  const bool down = !digitalRead(ENCSW_IN);
  const auto button = encoder.getButton();
  const auto edge = encoder.getButtonEvent();
  if (displayOK) {
    const bool buttonActivity = button != ClickEncoder::Open || edge != ClickEncoder::NoEvent;
    const auto idle = standby.poll(now, rotation != 0, down, buttonActivity);
    if (idle != PicoStandbyDisplay::Normal) {
      buttonArmed = !down;
      if (idle == PicoStandbyDisplay::Meter) standby.draw(display, now);
      else if (idle == PicoStandbyDisplay::Restore) {
        if (picoCVUiState != PICOPRO_CV_UI_OFF) PicoCVDrawOverlay(&menus[parameter()]);
        else draw();
      }
      return;
    }
  }
  if (edge == ClickEncoder::InActiveEdge) buttonArmed = true;
  const bool press = edge == ClickEncoder::ActiveEdge && buttonArmed;
  if (edge == ClickEncoder::ActiveEdge) buttonArmed = false;
  const uint8_t index = parameter();
  if (displayOK) {
    // Menus were bound in begin(). Only service/rebind an actual CV overlay.
    if (picoCVUiState != PICOPRO_CV_UI_OFF) {
      const uint8_t overlay = PicoCVServiceOverlay(menus, COUNT, rotation, down, button);
      if (overlay) { if (overlay == 2) draw(); return; }
    }
    if (assignable(index) && PicoCVHandleEntryButton(button, index)) {
      PicoCVDrawOverlay(&menus[index]); return;
    }
  }
  bool dirty = false;
  // Match shared menus: immediate press response, press-rotate acceleration,
  // no delayed single-click event or extra action on the release edge.
  if (press && !encoder.isPressRotateLocked()) {
    editing = !editing; dirty = true;
  }
  if (rotation) {
    if (encoder.isPressRotateLocked()) editing = true;
    if (editing) {
      const int32_t multiplier = encoder.isPressRotateLocked() ? encoder.getPressRotateMultiplier() : 1;
      values[index] = constrain(int32_t(values[index]) + int32_t(rotation) * multiplier * menus[index].step,
                                 int32_t(menus[index].min), int32_t(menus[index].max));
      if (index == CLOCK) PicoCVInputSelectClockCapture(values[CLOCK]);
      publish();
    } else {
      page = (page + rotation % kPageCount + kPageCount) % kPageCount;
    }
    dirty = true;
  }
  if (dirty || PicoCVShouldRefreshDisplay(parameter(), now)) draw();
}
