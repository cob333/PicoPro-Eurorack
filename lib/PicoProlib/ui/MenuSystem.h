#ifndef PICOPRO_MENU_SYSTEM_H_
#define PICOPRO_MENU_SYSTEM_H_

#include "../MenuTypes.h"
#include "KnobDisplay.h"
#include "CVModulation.h"

enum menumodes {
  PARAM_SELECT,
  PARAM_INPUT,
  WAITBUTTONRELEASE1,
  WAITBUTTONRELEASE2
};

static int16_t menustate = PARAM_SELECT;
int8_t menuindex = 0;

#define NUM_MENUS (sizeof(menus) / sizeof(menu))

#ifndef PICOPRO_MENU_DRAW_ITEM
#define PICOPRO_MENU_DRAW_ITEM(item, editing, nav_dir, index, total) \
  PicoKnobDrawMenuItem((item), (editing), (nav_dir), (index), (total))
#endif

void blankdisplay(void) {
  uint8_t tempbuf[SCREEN_BUFFER_SIZE];
  uint8_t *buf = display.getBuffer();
  memcpy(tempbuf, buf, SCREEN_BUFFER_SIZE);
  display.clearDisplay();
  display.display();
  memcpy(buf, tempbuf, SCREEN_BUFFER_SIZE);
}

void updatedisplay(void) {
  display.display();
  displaytimer = millis();
}

void drawmenu(int8_t index, bool editing = false) {
  PICOPRO_MENU_DRAW_ITEM(&menus[index], editing, 0, index, NUM_MENUS);
  updatedisplay();
}

void drawmenunav(int8_t index, int8_t nav_dir) {
  if (nav_dir != 0) {
    PICOPRO_MENU_DRAW_ITEM(&menus[index], false, nav_dir, index, NUM_MENUS);
    updatedisplay();
    delay(PICOPRO_KNOB_NAV_ANIM_MS);
  }
  PICOPRO_MENU_DRAW_ITEM(&menus[index], false, 0, index, NUM_MENUS);
  updatedisplay();
}

static inline int16_t scaledmenuinput(int16_t enc) {
  if (enc == 0 || !menuenc.isPressRotateLocked()) {
    return enc;
  }
  return enc * menuenc.getPressRotateMultiplier();
}

static inline bool applymenuinput(int16_t enc) {
  const int16_t scaled = scaledmenuinput(enc);
  if (scaled == 0) {
    return false;
  }
  int32_t temp = *menus[menuindex].parameter + (int32_t)scaled * menus[menuindex].step;
  if (temp < menus[menuindex].min) {
    temp = menus[menuindex].min;
  }
  if (temp > menus[menuindex].max) {
    temp = menus[menuindex].max;
  }
  if (temp == *menus[menuindex].parameter) {
    return false;
  }
  *menus[menuindex].parameter = (int16_t)temp;
  if (menus[menuindex].handler != 0) {
    (*menus[menuindex].handler)((int16_t)temp, menus[menuindex].parameter2);
  }
  return true;
}

void domenus(void) {
  int16_t enc = menuenc.getValue();
  static int16_t debouncecounter;
  const bool button_down = !digitalRead(ENCSW_IN);
  const ClickEncoder::Button button = menuenc.getButton();

  const uint8_t cv_result = PicoCVServiceOverlay(
      menus, NUM_MENUS, enc, button_down, button);
  if (cv_result != 0) {
    if (cv_result == 2) {
      drawmenu(menuindex, menustate == PARAM_INPUT);
    }
    return;
  }

  if (PicoCVHandleEntryButton(button, menuindex)) {
    PicoCVDrawOverlay(&menus[menuindex]);
    return;
  }

  switch (menustate) {
    case PARAM_SELECT:
      if (enc != 0) {
        menuindex += enc;
        if (menuindex < 0) {
          menuindex = NUM_MENUS - 1;
        }
        if (menuindex > (NUM_MENUS - 1)) {
          menuindex = 0;
        }
        drawmenunav(menuindex, enc);
      }
      if (button_down) {
        menustate = WAITBUTTONRELEASE1;
        debouncecounter = DEBOUNCE_CYCLES;
        drawmenu(menuindex, true);
      }
      break;
    case WAITBUTTONRELEASE1:
      if (enc != 0 || menuenc.isPressRotateLocked()) {
        menustate = PARAM_INPUT;
        debouncecounter = DEBOUNCE_CYCLES;
        if (enc != 0) {
          applymenuinput(enc);
        }
        drawmenu(menuindex, true);
        break;
      }
      if (!button_down) {
        --debouncecounter;
        if (debouncecounter == 0) {
          menustate = PARAM_INPUT;
        }
      }
      break;
    case PARAM_INPUT:
      if (applymenuinput(enc)) {
        drawmenu(menuindex, true);
      }
      if (button_down && !menuenc.isPressRotateLocked()) {
        debouncecounter = DEBOUNCE_CYCLES;
        menustate = WAITBUTTONRELEASE2;
      }
      break;
    case WAITBUTTONRELEASE2:
      if (enc != 0 || menuenc.isPressRotateLocked()) {
        menustate = PARAM_INPUT;
        debouncecounter = DEBOUNCE_CYCLES;
        if (enc != 0) {
          applymenuinput(enc);
        }
        drawmenu(menuindex, true);
        break;
      }
      if (!button_down) {
        --debouncecounter;
        if (debouncecounter <= 0) {
          menustate = PARAM_SELECT;
          drawmenu(menuindex, false);
        }
      }
      break;
    default:
      menustate = PARAM_SELECT;
      break;
  }

  const uint32_t now_ms = millis();
  bool refresh = PicoADSRShouldRefreshEnvelopeForMenu(&menus[menuindex], now_ms);
  if (!refresh) {
    refresh = PicoCVShouldRefreshDisplay(menuindex, now_ms);
  }
  if (refresh) {
    drawmenu(menuindex, menustate == PARAM_INPUT);
  }
}

#endif // PICOPRO_MENU_SYSTEM_H_
