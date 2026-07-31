// Stereo granular sample synthesizer for PicoPro.

#include "PicoPro.h"
#include <I2S.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ClickEncoder.h"
#include "PicoAppState.h"
#include "ADSR.h"
#include "GrainEngineTypes.h"
#include "grain_samples.h"

#define SAMPLERATE 44100
#define DEBOUNCE_CYCLES 100
#define GRAIN_VOICE_COUNT 4

enum UISTATES { RUN, DORMANT, WAIT_BUTTON_RELEASE };
int16_t UI_state = RUN;

I2S i2s(OUTPUT);
ClickEncoder menuenc(ENCA_IN, ENCB_IN, ENCSW_IN, ENCDIVIDE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
int32_t displaytimer;

#include "ui/CVModulation.h"

int16_t grain_sample = 0;
int16_t grain_freq = 200;
int16_t grain_size = 15;
int16_t grain_shape = 2;
int16_t grain_length = 250;
int16_t grain_position = 0;
int16_t grain_adsr_gate = 0;
int16_t grain_adsr_attack = 0;
int16_t grain_adsr_decay = 15;
int16_t grain_adsr_sustain = 80;
int16_t grain_adsr_release = 10;

static PicoADSRVoice grain_adsr_voice;
static volatile bool grain_audio_ready = false;
static volatile int16_t grain_exit_gain = 1000;

struct GrainControls {
  uint8_t sample;
  uint8_t shape;
  uint32_t phase_inc_q16;
  uint32_t duration_frames;
  uint16_t length_q10;
  uint16_t position_q10;
};

static volatile uint32_t grain_control_revision = 0;
static volatile GrainControls grain_controls = {0, 2, 32768, 5292, 250, 0};

#define GRAIN_STATE_TAG PICOPRO_APP_STATE_TAG('G', 'R', 'N', '1')
struct GrainState {
  int16_t sample;
  int16_t freq;
  int16_t size;
  int16_t shape;
  int16_t length;
  int16_t position;
  PicoADSRParams adsr;
  PicoCVPersistentState cv;
};

static void sanitizeGrainState() {
  grain_sample = constrain(grain_sample, 0,
                           static_cast<int16_t>(PICOPRO_GRAIN_SAMPLE_COUNT - 1));
  grain_freq = constrain(grain_freq, 20, 2000);
  grain_size = constrain(grain_size, 5, 500);
  grain_shape = constrain(grain_shape, 0, 3);
  grain_length = constrain(grain_length, 1, 1000);
  grain_position = constrain(grain_position, 0, 1000);
  grain_adsr_gate = constrain(grain_adsr_gate, 0, 2);
  grain_adsr_attack = constrain(grain_adsr_attack, 0, 100);
  grain_adsr_decay = constrain(grain_adsr_decay, 0, 100);
  grain_adsr_sustain = constrain(grain_adsr_sustain, 0, 100);
  grain_adsr_release = constrain(grain_adsr_release, 0, 100);
}

static bool loadGrainState() {
  GrainState state;
  if (!PicoAppStateLoad(GRAIN_STATE_TAG, &state, sizeof(state))) return false;
  grain_sample = state.sample;
  grain_freq = state.freq;
  grain_size = state.size;
  grain_shape = state.shape;
  grain_length = state.length;
  grain_position = state.position;
  grain_adsr_gate = state.adsr.gate;
  grain_adsr_attack = state.adsr.attack;
  grain_adsr_decay = state.adsr.decay;
  grain_adsr_sustain = state.adsr.sustain;
  grain_adsr_release = state.adsr.release;
  PicoCVImportState(&state.cv);
  sanitizeGrainState();
  return true;
}

static void saveGrainState() {
  sanitizeGrainState();
  GrainState state = {
    grain_sample, grain_freq, grain_size, grain_shape, grain_length,
    grain_position,
    {grain_adsr_gate, grain_adsr_attack, grain_adsr_decay,
     grain_adsr_sustain, grain_adsr_release}
  };
  PicoCVExportState(&state.cv);
  PicoAppStateSave(GRAIN_STATE_TAG, &state, sizeof(state));
}

void updateGrainMenu(int16_t, int16_t) { sanitizeGrainState(); }
#include "Grain_Menu.h"
#include "MenuSystem.h"

static const PicoADSRMenuBinding grainADSRMenus[] = {
  {&grain_adsr_gate, &grain_adsr_attack, &grain_adsr_decay,
   &grain_adsr_sustain, &grain_adsr_release}
};

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

static void publishGrainControls(const GrainControls &next) {
  ++grain_control_revision;
  grain_controls.sample = next.sample;
  grain_controls.shape = next.shape;
  grain_controls.phase_inc_q16 = next.phase_inc_q16;
  grain_controls.duration_frames = next.duration_frames;
  grain_controls.length_q10 = next.length_q10;
  grain_controls.position_q10 = next.position_q10;
  ++grain_control_revision;
}

static GrainControls readGrainControls() {
  GrainControls snapshot;
  while (true) {
    const uint32_t before = grain_control_revision;
    if (before & 1u) continue;
    snapshot.sample = grain_controls.sample;
    snapshot.shape = grain_controls.shape;
    snapshot.phase_inc_q16 = grain_controls.phase_inc_q16;
    snapshot.duration_frames = grain_controls.duration_frames;
    snapshot.length_q10 = grain_controls.length_q10;
    snapshot.position_q10 = grain_controls.position_q10;
    const uint32_t after = grain_control_revision;
    if (before == after && !(after & 1u)) return snapshot;
  }
}

static void serviceGrainControls() {
  static uint32_t last_ms = 0;
  static int16_t last_gate = -1, last_attack = -1, last_decay = -1;
  static int16_t last_sustain = -1, last_release = -1;
  const uint32_t now = millis();
  if ((now - last_ms) < 2) return;
  last_ms = now;

  GrainControls next;
  next.sample = PicoCVModulatedValue(0, grain_sample,
                                      menus[0].min, menus[0].max);
  const float frequency = PicoCVModulatedFrequencyHz(
      1, static_cast<float>(grain_freq),
      static_cast<float>(menus[1].min), static_cast<float>(menus[1].max));
  next.phase_inc_q16 = static_cast<uint32_t>(
      constrain(frequency / 440.0f, 0.01f, 8.0f) * 65536.0f);
  const int16_t size_ms = PicoCVModulatedValue(
      2, grain_size, menus[2].min, menus[2].max);
  next.duration_frames = max(1u, static_cast<uint32_t>(size_ms) *
                                  static_cast<uint32_t>(SAMPLERATE) / 1000u);
  next.shape = PicoCVModulatedValue(3, grain_shape,
                                     menus[3].min, menus[3].max);
  next.length_q10 = PicoCVModulatedValue(4, grain_length,
                                          menus[4].min, menus[4].max);
  next.position_q10 = PicoCVModulatedValue(5, grain_position,
                                            menus[5].min, menus[5].max);
  publishGrainControls(next);

  const int16_t gate = grain_adsr_gate;
  const int16_t attack = PicoCVModulatedValue(
      7, grain_adsr_attack, menus[7].min, menus[7].max);
  const int16_t decay = PicoCVModulatedValue(
      8, grain_adsr_decay, menus[8].min, menus[8].max);
  const int16_t sustain = PicoCVModulatedValue(
      9, grain_adsr_sustain, menus[9].min, menus[9].max);
  const int16_t release = PicoCVModulatedValue(
      10, grain_adsr_release, menus[10].min, menus[10].max);
  if (gate != last_gate || attack != last_attack || decay != last_decay ||
      sustain != last_sustain || release != last_release) {
    PicoADSRApplyParams(&grain_adsr_voice, gate, attack, decay, sustain,
                        release, SAMPLERATE);
    last_gate = gate;
    last_attack = attack;
    last_decay = decay;
    last_sustain = sustain;
    last_release = release;
  }
}

static void prepareGrainExit() {
  grain_exit_gain = 0;
  delay(32);
}

static uint16_t grainWindow(const GrainVoice &voice) {
  if (voice.duration <= 1) return 0;
  const uint32_t phase = min(65535u,
      static_cast<uint32_t>((static_cast<uint64_t>(voice.age) * 65535u) /
                            (voice.duration - 1u)));
  switch (voice.shape) {
    case 1:  // triangle
      return phase < 32768u ? phase * 2u : (65535u - phase) * 2u;
    case 2: {  // Hann-like raised cosine, using a cheap parabolic curve
      const uint32_t x = phase < 32768u ? phase : 65535u - phase;
      return static_cast<uint16_t>((static_cast<uint64_t>(x) *
                                    (65535u - x) * 4u) >> 16);
    }
    case 3: {  // Tukey: flat center with quarter-length fades
      if (phase < 16384u) return phase * 4u;
      if (phase > 49151u) return (65535u - phase) * 4u;
      return 65535u;
    }
    default: {  // rectangular with a short click-suppression edge
      constexpr uint32_t edge_frames = 32u;
      const uint32_t remaining = voice.duration - 1u - voice.age;
      const uint32_t edge_age = min(voice.age, remaining);
      return edge_age >= edge_frames
                 ? 65535u
                 : static_cast<uint16_t>((edge_age * 65535u) / edge_frames);
    }
  }
}

static int16_t interpolateSample(const PicoGrainSample &sample,
                                 const GrainVoice &voice,
                                 uint64_t frame_q16, uint8_t channel) {
  const uint32_t frame = static_cast<uint32_t>(frame_q16 >> 16);
  const uint16_t fraction = frame_q16 & 0xffffu;
  const uint32_t region_end = voice.region_start + voice.region_frames;
  const uint32_t next = frame + 1u < region_end
                            ? frame + 1u
                            : voice.region_start;
  const int16_t a = sample.data[frame * 2u + channel];
  const int16_t b = sample.data[next * 2u + channel];
  return a + ((static_cast<int32_t>(b - a) * fraction) >> 16);
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
  i2s.setBCLK(BCLK);
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  i2s.setFrequency(SAMPLERATE);
  i2s.begin();

  PicoADSRInitVoice(&grain_adsr_voice);
  PicoCVBindMenus(menus, NUM_MENUS);
  if (!loadGrainState()) {
    PicoCVCommitAssignment(1, 1, PICOPRO_CV_VOCT_AMOUNT);
  }
  serviceGrainControls();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) while (true) {}
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  displaytimer = millis();
  PicoADSRRegisterMenus(grainADSRMenus, 1);
  drawmenu(0);
  menuenc.getValue();
  grain_audio_ready = true;
}

void loop() {
  static int16_t debounce_counter = 0;
  PicoProServiceSelectorExit(ENCSW_IN, menuenc, saveGrainState, prepareGrainExit);
  PicoADSRServiceGate(&grain_adsr_voice, millis());
  serviceGrainControls();
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
  while (!grain_audio_ready) tight_loop_contents();
}

void loop1() {
  static GrainControls controls = {0, 2, 32768, 5292, 250, 0};
  static GrainVoice voices[GRAIN_VOICE_COUNT] = {};
  static uint32_t spawn_countdown = 0;
  static uint32_t scan_cursor = 0;
  static uint8_t next_voice = 0;
  static uint8_t last_sample = 0xffu;
  static uint8_t control_divider = 0;
  static int16_t exit_gain = 1000;

  if (++control_divider >= 32u) {
    control_divider = 0;
    controls = readGrainControls();
  }
  const PicoGrainSample &sample = PICOPRO_GRAIN_SAMPLES[controls.sample];
  if (controls.sample != last_sample) {
    for (uint8_t i = 0; i < GRAIN_VOICE_COUNT; ++i) voices[i].active = false;
    spawn_countdown = 0;
    scan_cursor = 0;
    last_sample = controls.sample;
  }

  if (spawn_countdown == 0) {
    const uint32_t start = static_cast<uint32_t>(
        (static_cast<uint64_t>(sample.frames - 1u) * controls.position_q10) /
        1000u);
    const uint32_t available = sample.frames - start;
    const uint32_t region = max(2u, static_cast<uint32_t>(
        (static_cast<uint64_t>(available) * controls.length_q10) / 1000u));
    GrainVoice &voice = voices[next_voice];
    voice.region_start = start;
    voice.region_frames = min(region, available);
    voice.phase_q16 = static_cast<uint64_t>(
        start + scan_cursor % voice.region_frames) << 16;
    voice.age = 0;
    voice.duration = controls.duration_frames;
    voice.phase_inc_q16 = controls.phase_inc_q16;
    voice.shape = controls.shape;
    voice.active = true;
    scan_cursor = (scan_cursor + max(1u, voice.region_frames / 7u)) %
                  voice.region_frames;
    next_voice = (next_voice + 1u) % GRAIN_VOICE_COUNT;
    spawn_countdown = max(1u, controls.duration_frames / 2u);
  }
  --spawn_countdown;

  int64_t sum_left = 0;
  int64_t sum_right = 0;
  for (uint8_t i = 0; i < GRAIN_VOICE_COUNT; ++i) {
    GrainVoice &voice = voices[i];
    if (!voice.active) continue;
    const uint32_t region_end = voice.region_start + voice.region_frames;
    uint32_t frame = static_cast<uint32_t>(voice.phase_q16 >> 16);
    if (frame >= region_end) {
      frame = voice.region_start + (frame - voice.region_start) % voice.region_frames;
      voice.phase_q16 = (static_cast<uint64_t>(frame) << 16) |
                        (voice.phase_q16 & 0xffffu);
    }
    const uint16_t window = grainWindow(voice);
    sum_left += static_cast<int32_t>(
        interpolateSample(sample, voice, voice.phase_q16, 0)) * window;
    sum_right += static_cast<int32_t>(
        interpolateSample(sample, voice, voice.phase_q16, 1)) * window;
    voice.phase_q16 += voice.phase_inc_q16;
    if (++voice.age >= voice.duration) voice.active = false;
  }

  const int16_t amp = PicoADSRApplyAmp(&grain_adsr_voice, 100);
  if (exit_gain > grain_exit_gain) --exit_gain;
  const int32_t gain = amp * exit_gain;
  int64_t left = ((sum_left >> 16) * gain) / 200000;
  int64_t right = ((sum_right >> 16) * gain) / 200000;
  left = constrain(left, static_cast<int64_t>(-32768), static_cast<int64_t>(32767));
  right = constrain(right, static_cast<int64_t>(-32768), static_cast<int64_t>(32767));
  i2s.write(static_cast<int32_t>(left * 65536));
  i2s.write(static_cast<int32_t>(right * 65536));
}
