/*
PicoPro Calibration

CV input calibration app:
- CV1 0V / 1V / 2V / 3V
- CV2 0V / 1V / 2V / 3V
- save to boot config calibration record
*/

#include "PicoPro.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ClickEncoder.h"

#define CAL_DEBOUNCE_CYCLES 80
#define CAL_SAMPLE_READS 32

ClickEncoder menuenc(ENCA_IN, ENCB_IN, ENCSW_IN, ENCDIVIDE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

enum CalibrationStep {
  CAL_CV1_ZERO = 0,
  CAL_CV1_ONE,
  CAL_CV1_TWO,
  CAL_CV1_THREE,
  CAL_CV2_ZERO,
  CAL_CV2_ONE,
  CAL_CV2_TWO,
  CAL_CV2_THREE,
  CAL_SAVE,
  CAL_STEP_COUNT
};

static PicoBootCalibration cal;
static float cv_counts[2][4] = {
  {4095.0f, 3512.0f, 2930.0f, 2347.0f},
  {4095.0f, 3512.0f, 2930.0f, 2347.0f},
};
static bool captured[2][4] = {
  {false, false, false, false},
  {false, false, false, false},
};
static uint8_t step = CAL_CV1_ZERO;
static uint32_t last_draw_ms = 0;
static char status_text[14] = "ready";

static void alarm_in_us_arm(uint32_t delay_us);
static void alarm_irq(void);

static void alarm_in_us(uint32_t delay_us) {
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
  irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
  irq_set_enabled(ALARM_IRQ, true);
  alarm_in_us_arm(delay_us);
}

static void alarm_in_us_arm(uint32_t delay_us) {
  uint64_t target = timer_hw->timerawl + delay_us;
  timer_hw->alarm[ALARM_NUM] = (uint32_t)target;
}

static void alarm_irq(void) {
  menuenc.service();
  hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
  alarm_in_us_arm(TIMER_MICROS);
}

static uint16_t sampleAverage(uint8_t cv_index) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < CAL_SAMPLE_READS; ++i) {
    sum += cv_index == 0 ? sampleCV1() : sampleCV2();
    delay(1);
  }
  return (uint16_t)((sum + (CAL_SAMPLE_READS / 2)) / CAL_SAMPLE_READS);
}

static uint16_t liveRawForStep(void) {
  return step >= CAL_CV2_ZERO && step <= CAL_CV2_THREE ? sampleCV2() : sampleCV1();
}

static bool calibrationReady(void) {
  for (uint8_t cv = 0; cv < 2; ++cv) {
    for (uint8_t volt = 0; volt < 4; ++volt) {
      if (!captured[cv][volt]) {
        return false;
      }
    }
  }
  return true;
}

static void fitCalibration(uint8_t cv, float *zero_counts, float *counts_per_volt) {
  float sum_y = 0.0f;
  float sum_xy = 0.0f;
  for (uint8_t volt = 0; volt < 4; ++volt) {
    sum_y += cv_counts[cv][volt];
    sum_xy += (float)volt * cv_counts[cv][volt];
  }

  const float slope = ((4.0f * sum_xy) - (6.0f * sum_y)) / 20.0f;
  const float zero = (sum_y - (slope * 6.0f)) * 0.25f;
  float cpv = -slope;
  if (cpv < 1.0f) {
    cpv = 1.0f;
  }

  *zero_counts = zero;
  *counts_per_volt = cpv;
}

static void copyStatus(const char *text) {
  uint8_t i = 0;
  while (text[i] != 0 && i < sizeof(status_text) - 1) {
    status_text[i] = text[i];
    ++i;
  }
  status_text[i] = 0;
}

static void drawTextCentered(const char *text, int16_t y) {
  int16_t len = 0;
  while (text[len] != 0 && len < 12) {
    ++len;
  }
  int16_t x = (SCREEN_WIDTH - len * 6) / 2;
  if (x < 0) {
    x = 0;
  }
  display.setCursor(x, y);
  display.write((const uint8_t *)text, len);
}

static const char *stepTitle(void) {
  switch (step) {
    case CAL_CV1_ZERO: return "cv1 0v";
    case CAL_CV1_ONE: return "cv1 1v";
    case CAL_CV1_TWO: return "cv1 2v";
    case CAL_CV1_THREE: return "cv1 3v";
    case CAL_CV2_ZERO: return "cv2 0v";
    case CAL_CV2_ONE: return "cv2 1v";
    case CAL_CV2_TWO: return "cv2 2v";
    case CAL_CV2_THREE: return "cv2 3v";
    default: return "save";
  }
}

static void drawScreen(void) {
  char line[16];
  const uint16_t raw = liveRawForStep();

  display.clearDisplay();
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  drawTextCentered(stepTitle(), 0);

  if (step == CAL_SAVE) {
    float zero;
    float cpv;
    fitCalibration(0, &zero, &cpv);
    snprintf(line, sizeof(line), "c1 %d", (int)cpv);
    drawTextCentered(line, 10);
    fitCalibration(1, &zero, &cpv);
    snprintf(line, sizeof(line), "c2 %d", (int)cpv);
    drawTextCentered(line, 18);
  } else {
    snprintf(line, sizeof(line), "adc %u", raw);
    drawTextCentered(line, 10);
    drawTextCentered("press set", 18);
  }

  drawTextCentered(status_text, 25);
  display.display();
  last_draw_ms = millis();
}

static void captureCurrentStep(void) {
  uint8_t cv = 0;
  uint8_t volt = 0;

  switch (step) {
    case CAL_CV1_ZERO:
      cv = 0;
      volt = 0;
      break;
    case CAL_CV1_ONE:
      cv = 0;
      volt = 1;
      break;
    case CAL_CV1_TWO:
      cv = 0;
      volt = 2;
      break;
    case CAL_CV1_THREE:
      cv = 0;
      volt = 3;
      break;
    case CAL_CV2_ZERO:
      cv = 1;
      volt = 0;
      break;
    case CAL_CV2_ONE:
      cv = 1;
      volt = 1;
      break;
    case CAL_CV2_TWO:
      cv = 1;
      volt = 2;
      break;
    case CAL_CV2_THREE:
      cv = 1;
      volt = 3;
      break;
    default:
      return;
  }

  cv_counts[cv][volt] = sampleAverage(cv);
  captured[cv][volt] = true;
  char text[14];
  snprintf(text, sizeof(text), "set c%d %d", cv + 1, volt);
  copyStatus(text);
  step = (uint8_t)(step + 1u);
  if (step >= CAL_SAVE) {
    step = CAL_SAVE;
  }
}

static void saveCalibration(void) {
  if (!calibrationReady()) {
    copyStatus("need all");
    return;
  }

  fitCalibration(0, &cal.cv1_zero_counts, &cal.cv1_counts_per_volt);
  fitCalibration(1, &cal.cv2_zero_counts, &cal.cv2_counts_per_volt);

  if (PicoBootSaveCalibration(&cal)) {
    copyStatus("saved");
  } else {
    copyStatus("save err");
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
  PicoBootLoadCalibration(&cal);
  for (uint8_t volt = 0; volt < 4; ++volt) {
    cv_counts[0][volt] = cal.cv1_zero_counts - (cal.cv1_counts_per_volt * (float)volt);
    cv_counts[1][volt] = cal.cv2_zero_counts - (cal.cv2_counts_per_volt * (float)volt);
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    while (true) {
      delay(1);
    }
  }
  drawScreen();
  menuenc.getValue();
}

void loop() {
  static int16_t debounce = 0;
  PicoProServiceSelectorExit(ENCSW_IN, menuenc);

  const int16_t enc = menuenc.getValue();
  if (enc != 0) {
    int16_t next = (int16_t)step + enc;
    while (next < 0) {
      next += CAL_STEP_COUNT;
    }
    while (next >= CAL_STEP_COUNT) {
      next -= CAL_STEP_COUNT;
    }
    step = (uint8_t)next;
    copyStatus("ready");
    drawScreen();
  }

  const ClickEncoder::Button button = menuenc.getButton();
  if (button == ClickEncoder::Clicked && debounce == 0) {
    if (step == CAL_SAVE) {
      saveCalibration();
    } else {
      captureCurrentStep();
    }
    debounce = CAL_DEBOUNCE_CYCLES;
    drawScreen();
  }

  if (debounce > 0) {
    --debounce;
  }
  if ((millis() - last_draw_ms) > 120) {
    drawScreen();
  }
  delay(1);
}
