#ifndef PICOPRO_KNOB_DISPLAY_H_
#define PICOPRO_KNOB_DISPLAY_H_

#include <Arduino.h>
#include "../MenuTypes.h"
#include "CVModulation.h"
#include "ADSRDisplay.h"
#include "../../../Fonts/Picopixel.h"

#ifndef PICOPRO_KNOB_SCREEN_WIDTH
#define PICOPRO_KNOB_SCREEN_WIDTH 64
#endif

#ifndef PICOPRO_KNOB_SCREEN_HEIGHT
#define PICOPRO_KNOB_SCREEN_HEIGHT 32
#endif

#define PICOPRO_KNOB_CENTER_X 32
#define PICOPRO_KNOB_CENTER_Y 10
#define PICOPRO_KNOB_RADIUS 8
#define PICOPRO_KNOB_LEFT_ARROW_X 7
#define PICOPRO_KNOB_RIGHT_ARROW_X 56
#define PICOPRO_KNOB_ARROW_Y 16
#define PICOPRO_KNOB_NAV_ANIM_MS 18
#define PICOPRO_KNOB_INDEX_X 0
#define PICOPRO_KNOB_INDEX_Y 6
#define PICOPRO_KNOB_INDEX_BOX_Y 0
#define PICOPRO_KNOB_INDEX_BOX_H 9
#define PICOPRO_KNOB_INDEX_TEXT_X 2
#define PICOPRO_KNOB_INDEX_CHAR_W 4
#define PICOPRO_KNOB_CV_BOX_Y 0
#define PICOPRO_KNOB_CV_BOX_W 7
#define PICOPRO_KNOB_CV_BOX_H 9
#define PICOPRO_KNOB_CV1_X 49
#define PICOPRO_KNOB_CV2_X 57
#define PICOPRO_KNOB_CV_TEXT_Y 6
#define PICOPRO_KNOB_TEXT_Y 24
#define PICOPRO_KNOB_TEXT_WIDTH 6
#define PICOPRO_KNOB_TEXT_CHARS 10

typedef struct {
  int8_t x;
  int8_t y;
} PicoKnobPoint;

static const PicoKnobPoint PICOPRO_KNOB_NEEDLE_POINTS[31] = {
  {-3,  6}, {-4,  5}, {-5,  4}, {-6,  3}, {-7,  2}, {-7,  1},
  {-7,  0}, {-7, -1}, {-7, -2}, {-6, -4}, {-5, -4}, {-4, -5},
  {-3, -6}, {-2, -7}, {-1, -7}, { 0, -7}, { 1, -7}, { 2, -7},
  { 3, -6}, { 4, -5}, { 5, -4}, { 6, -4}, { 7, -2}, { 7, -1},
  { 7,  0}, { 7,  1}, { 7,  2}, { 6,  3}, { 5,  4}, { 4,  5},
  { 3,  6},
};

static inline char PicoKnobLowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static inline int16_t PicoKnobClampValue(int16_t value, int16_t min_value, int16_t max_value) {
  if (value < min_value) return min_value;
  if (value > max_value) return max_value;
  return value;
}

static inline uint8_t PicoKnobNeedleIndex(int16_t value, int16_t min_value, int16_t max_value) {
  if (max_value <= min_value) return 0;
  value = PicoKnobClampValue(value, min_value, max_value);
  return (uint8_t)(((int32_t)(value - min_value) * 30 + ((max_value - min_value) / 2)) /
                   (max_value - min_value));
}

static inline void PicoKnobValueText(const menu *item, char *out, size_t len) {
  if (len == 0) return;
  int16_t value = *item->parameter;

  switch (item->ptype) {
    case TYPE_INTEGER:
      snprintf(out, len, "%d", value);
      break;
    case TYPE_FLOAT: {
      const bool neg = value < 0;
      uint16_t abs_value = neg ? (uint16_t)(-value) : (uint16_t)value;
      uint16_t whole = abs_value / 1000;
      uint16_t frac = (abs_value % 1000 + 5) / 10;
      if (frac >= 100) {
        frac = 0;
        ++whole;
      }
      snprintf(out, len, neg ? "-%u.%02u" : "%u.%02u", whole, frac);
      break;
    }
    case TYPE_TEXT:
    case TYPE_SAMPLENAME:
      value = PicoKnobClampValue(value, 0, item->max);
      if (item->ptext != 0 && item->ptext[value] != 0) {
        snprintf(out, len, "%s", item->ptext[value]);
      } else {
        snprintf(out, len, "%d", value);
      }
      break;
    default:
    case TYPE_NONE:
      snprintf(out, len, "-");
      break;
  }
}

static inline void PicoKnobCompactLabel(const char *name, const char *value, char *out, size_t len) {
  if (len == 0) return;

  const uint8_t max_chars = (len - 1) < PICOPRO_KNOB_TEXT_CHARS ? (len - 1) : PICOPRO_KNOB_TEXT_CHARS;
  uint8_t value_len = strlen(value);

  if (value_len > PICOPRO_KNOB_TEXT_CHARS - 2) value_len = PICOPRO_KNOB_TEXT_CHARS - 2;
  const uint8_t max_name_len = PICOPRO_KNOB_TEXT_CHARS - value_len - 1;
  uint8_t out_len = 0;

  for (const char *p = name; *p != 0 && out_len < max_name_len && out_len < max_chars; ++p) {
    if (*p != ' ') out[out_len++] = PicoKnobLowerAscii(*p);
  }

  if (out_len < max_chars) {
    out[out_len++] = ':';
  }

  for (uint8_t i = 0; i < value_len && out_len < max_chars; ++i) {
    out[out_len++] = PicoKnobLowerAscii(value[i]);
  }

  out[out_len] = 0;
}

static inline void PicoKnobDrawTextCentered(const char *text) {
  uint8_t len = 0;
  while (text[len] != 0 && len < PICOPRO_KNOB_TEXT_CHARS) {
    ++len;
  }
  display.setCursor((PICOPRO_KNOB_SCREEN_WIDTH - ((int16_t)len * PICOPRO_KNOB_TEXT_WIDTH)) / 2,
                    PICOPRO_KNOB_TEXT_Y);
  display.write((const uint8_t *)text, len);
}

static inline void PicoKnobDrawArrow(int16_t x, int16_t y, bool right) {
  if (right) {
    display.drawLine(x, y - 3, x + 3, y, WHITE);
    display.drawLine(x + 3, y, x, y + 3, WHITE);
  } else {
    display.drawLine(x + 3, y - 3, x, y, WHITE);
    display.drawLine(x, y, x + 3, y + 3, WHITE);
  }
}

static inline void PicoKnobWriteU8(uint8_t value) {
  if (value >= 100) {
    display.write((uint8_t)('0' + value / 100));
    value %= 100;
    display.write((uint8_t)('0' + value / 10));
  } else if (value >= 10) {
    display.write((uint8_t)('0' + value / 10));
  }
  display.write((uint8_t)('0' + value % 10));
}

static inline uint8_t PicoKnobU8Chars(uint8_t value) {
  return value >= 100 ? 3 : (value >= 10 ? 2 : 1);
}

static inline void PicoKnobDrawIndex(uint8_t index, uint8_t total) {
  if (total == 0) return;
  const uint8_t chars = PicoKnobU8Chars((uint8_t)(index + 1)) + 1 + PicoKnobU8Chars(total);
  const uint8_t box_w = (uint8_t)(chars * PICOPRO_KNOB_INDEX_CHAR_W + 3);
  display.drawRect(PICOPRO_KNOB_INDEX_X, PICOPRO_KNOB_INDEX_BOX_Y,
                   box_w, PICOPRO_KNOB_INDEX_BOX_H, WHITE);
  display.setFont(&Picopixel);
  display.setTextColor(WHITE, BLACK);
  display.setCursor(PICOPRO_KNOB_INDEX_X + PICOPRO_KNOB_INDEX_TEXT_X,
                    PICOPRO_KNOB_INDEX_Y);
  PicoKnobWriteU8((uint8_t)(index + 1));
  display.write((uint8_t)'/');
  PicoKnobWriteU8(total);
  display.setFont(NULL);
}

static inline void PicoKnobDrawCVBox(int16_t x, char label, bool active) {
  if (active) {
    display.fillRect(x, PICOPRO_KNOB_CV_BOX_Y,
                     PICOPRO_KNOB_CV_BOX_W, PICOPRO_KNOB_CV_BOX_H, WHITE);
  } else {
    display.drawRect(x, PICOPRO_KNOB_CV_BOX_Y,
                     PICOPRO_KNOB_CV_BOX_W, PICOPRO_KNOB_CV_BOX_H, WHITE);
  }
  display.setTextColor(active ? BLACK : WHITE, active ? WHITE : BLACK);
  display.setCursor(x + 2, PICOPRO_KNOB_CV_TEXT_Y);
  display.write((uint8_t)label);
}

static inline void PicoKnobDrawCVIndicator(uint8_t index) {
  const int8_t active_input = PicoCVActiveInput(index);
  display.setFont(&Picopixel);
  if (active_input == PICOPRO_CV_INPUT_LFO) {
    PicoKnobDrawCVBox(PICOPRO_KNOB_CV1_X, 'L', true);
    PicoKnobDrawCVBox(PICOPRO_KNOB_CV2_X, 'F', true);
  } else {
    PicoKnobDrawCVBox(PICOPRO_KNOB_CV1_X, '1', active_input == 0);
    PicoKnobDrawCVBox(PICOPRO_KNOB_CV2_X, '2', active_input == 1);
  }
  display.setTextColor(WHITE, BLACK);
  display.setFont(NULL);
}

static inline void PicoKnobDrawMenuItem(
    const menu *item,
    bool editing = false,
    int8_t nav_dir = 0,
    uint8_t index = 0,
    uint8_t total = 0) {
  char value[12];
  char label[PICOPRO_KNOB_TEXT_CHARS + 1];
  const int16_t display_value = PicoCVDisplayValue(index, *item->parameter);
  const uint8_t needle = PicoKnobNeedleIndex(display_value, item->min, item->max);
  const PicoKnobPoint point = PICOPRO_KNOB_NEEDLE_POINTS[needle];
  const int16_t left_x = PICOPRO_KNOB_LEFT_ARROW_X + (nav_dir < 0 ? -2 : 0);
  const int16_t right_x = PICOPRO_KNOB_RIGHT_ARROW_X + (nav_dir > 0 ? 2 : 0);

  display.clearDisplay();
  PicoKnobDrawIndex(index, total);
  PicoKnobDrawCVIndicator(index);
  PicoKnobDrawArrow(left_x, PICOPRO_KNOB_ARROW_Y, false);
  PicoKnobDrawArrow(right_x, PICOPRO_KNOB_ARROW_Y, true);
  if (!PicoADSRDrawEnvelopeForMenu(item)) {
    display.drawCircle(PICOPRO_KNOB_CENTER_X, PICOPRO_KNOB_CENTER_Y,
                       PICOPRO_KNOB_RADIUS, WHITE);
    display.drawPixel(PICOPRO_KNOB_CENTER_X, PICOPRO_KNOB_CENTER_Y, WHITE);
    display.drawLine(PICOPRO_KNOB_CENTER_X, PICOPRO_KNOB_CENTER_Y,
                     PICOPRO_KNOB_CENTER_X + point.x,
                     PICOPRO_KNOB_CENTER_Y + point.y, WHITE);
  }

  PicoKnobValueText(item, value, sizeof(value));
  PicoKnobCompactLabel(item->name, value, label, sizeof(label));
  if (editing) {
    display.fillRect(0, 23, PICOPRO_KNOB_SCREEN_WIDTH, 9, WHITE);
    display.setTextColor(BLACK, WHITE);
  } else {
    display.setTextColor(WHITE, BLACK);
  }
  PicoKnobDrawTextCentered(label);
  if (editing) {
    display.setTextColor(WHITE, BLACK);
  }
}

#endif // PICOPRO_KNOB_DISPLAY_H_
