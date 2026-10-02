// ----------------------------------------------------------------------------
// Rotary Encoder Driver with Acceleration
// Supports Click, DoubleClick, Long Click
//
// (c) 2010 karl@pitrich.com
// (c) 2014 karl@pitrich.com
//
// Timer-based rotary encoder logic by Peter Dannegger
// http://www.mikrocontroller.net/articles/Drehgeber
// ----------------------------------------------------------------------------
// RH added button edge events which are needed sometimes Dec 2025

#include "ClickEncoder.h"

// ----------------------------------------------------------------------------
// Button configuration (values for 1ms timer service calls)
//
#define ENC_BUTTONINTERVAL    10  // check button every x milliseconds, also debouce time
#define ENC_DOUBLECLICKTIME  250  // second click within this window
#define ENC_CLICKMINTIME      20  // ignore switch bounce shorter than this
#define ENC_CLICKMAXTIME     260  // longer presses are not clicks
#define ENC_HOLDTIME        500  // report held button after .5s

// ----------------------------------------------------------------------------
// Acceleration configuration (for 1000Hz calls to ::service())
//
#define ENC_ACCEL_TOP      3072   // max. acceleration: *12 (val >> 8)
#define ENC_ACCEL_INC        50
#define ENC_ACCEL_DEC         2

#define ENC_PRESSROTATE_MULTIPLIER 10
#define ENC_PRESSROTATE_ACCEL_TOP  3072
#define ENC_PRESSROTATE_ACCEL_INC    80
#define ENC_PRESSROTATE_ACCEL_DEC     2

// ----------------------------------------------------------------------------

#if ENC_DECODER != ENC_NORMAL
#  ifdef ENC_HALFSTEP
     // decoding table for hardware with flaky notch (half resolution)
     const int8_t ClickEncoder::table[16] __attribute__((__progmem__)) = {
       0, 0, -1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, -1, 0, 0
     };
#  else
     // decoding table for normal hardware
     const int8_t ClickEncoder::table[16] __attribute__((__progmem__)) = {
       0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0
     };
#  endif
#endif

// ----------------------------------------------------------------------------

ClickEncoder::ClickEncoder(uint8_t A, uint8_t B, uint8_t BTN, uint8_t stepsPerNotch, bool active)
  : doubleClickEnabled(true), accelerationEnabled(false),
    delta(0), last(0), acceleration(0),
    button(Open), steps(stepsPerNotch),
    pinA(A), pinB(B), pinBTN(BTN), pinsActive(active)
{
  uint8_t configType = (pinsActive == LOW) ? INPUT_PULLUP : INPUT;
  pinMode(pinA, configType);
  pinMode(pinB, configType);
  pinMode(pinBTN, configType);

  if (digitalRead(pinA) == pinsActive) {
    last = 3;
  }

  if (digitalRead(pinB) == pinsActive) {
    last ^=1;
  }
}

// ----------------------------------------------------------------------------
// call this every 1 millisecond via timer ISR
//
void ClickEncoder::service(void)
{
  bool moved = false;

  if (accelerationEnabled) { // decelerate every tick
    acceleration -= ENC_ACCEL_DEC;
    if (acceleration & 0x8000) { // handle overflow of MSB is set
      acceleration = 0;
    }
  }

#ifndef WITHOUT_BUTTON
  if (pressRotateAcceleration > ENC_PRESSROTATE_ACCEL_DEC) {
    pressRotateAcceleration -= ENC_PRESSROTATE_ACCEL_DEC;
  }
  else {
    pressRotateAcceleration = 0;
  }
#endif

#if ENC_DECODER == ENC_FLAKY
  last = (last << 2) & 0x0F;

  if (digitalRead(pinA) == pinsActive) {
    last |= 2;
  }

  if (digitalRead(pinB) == pinsActive) {
    last |= 1;
  }

  uint8_t tbl = pgm_read_byte(&table[last]);
  if (tbl) {
    delta += tbl;
    moved = true;
  }
#elif ENC_DECODER == ENC_NORMAL
  int8_t curr = 0;

  if (digitalRead(pinA) == pinsActive) {
    curr = 3;
  }

  if (digitalRead(pinB) == pinsActive) {
    curr ^= 1;
  }

  int8_t diff = last - curr;

  if (diff & 1) {            // bit 0 = step
    last = curr;
    delta += (diff & 2) - 1; // bit 1 = direction (+/-)
    moved = true;
  }
#else
# error "Error: define ENC_DECODER to ENC_NORMAL or ENC_FLAKY"
#endif

  if (accelerationEnabled && moved) {
    // increment accelerator if encoder has been moved
    if (acceleration <= (ENC_ACCEL_TOP - ENC_ACCEL_INC)) {
      acceleration += ENC_ACCEL_INC;
    }
  }

#ifndef WITHOUT_BUTTON
  if (moved && pinBTN > 0 && (digitalRead(pinBTN) == pinsActive)) {
    pressRotateLocked = true;
    doubleClickTicks = 0;
    event = PressRotateStart;
    if (pressRotateAcceleration <= (ENC_PRESSROTATE_ACCEL_TOP - ENC_PRESSROTATE_ACCEL_INC)) {
      pressRotateAcceleration += ENC_PRESSROTATE_ACCEL_INC;
    }
    else {
      pressRotateAcceleration = ENC_PRESSROTATE_ACCEL_TOP;
    }
  }
#endif

  // handle button
  //
#ifndef WITHOUT_BUTTON
  if (pinBTN > 0 && ++buttonCheckTicks >= ENC_BUTTONINTERVAL) {
    buttonCheckTicks = 0;
    const bool buttonActive = (digitalRead(pinBTN) == pinsActive);


// RH added events that flag button activating and deactivating
    if (buttonActive && !edge) { // event - key has just activated
      edge=1;
      event=ActiveEdge;
    }

    if (!buttonActive && edge) { // event - key has just deactivated
      edge=0;
      event=InActiveEdge;
    }

    if (buttonActive) { // key is down
      keyDownTicks++;
      if (pressRotateLocked) {
        button = PressedRotate;
      }
      else if (keyDownTicks > (ENC_HOLDTIME / ENC_BUTTONINTERVAL)) {
        button = Held;
      }
      else {
        button=Closed;
      }
    }

    if (!buttonActive) { // key is now up
      if (keyDownTicks /*> ENC_BUTTONINTERVAL*/) {
        if (pressRotateLocked) {
          button = Released;
          doubleClickTicks = 0;
        }
        else if (button == Held) {
          button = Released;
          doubleClickTicks = 0;
        }
        else {
          const uint16_t clickMinTicks = ENC_CLICKMINTIME / ENC_BUTTONINTERVAL;
          const uint16_t clickMaxTicks = ENC_CLICKMAXTIME / ENC_BUTTONINTERVAL;
          if (keyDownTicks < clickMinTicks || keyDownTicks > clickMaxTicks) {
            button = Released;
            doubleClickTicks = 0;
          }
          else {
#define ENC_SINGLECLICKONLY 1
            if (doubleClickTicks > ENC_SINGLECLICKONLY) {   // prevent trigger in single click mode
              if (doubleClickTicks < (ENC_DOUBLECLICKTIME / ENC_BUTTONINTERVAL)) {
                button = DoubleClicked;
                doubleClickTicks = 0;
              }
            }
            else {
              doubleClickTicks = (doubleClickEnabled) ? (ENC_DOUBLECLICKTIME / ENC_BUTTONINTERVAL) : ENC_SINGLECLICKONLY;
            }
          }
        }
      }

      keyDownTicks = 0;
      pressRotateLocked = false;
      pressRotateAcceleration = 0;
    }

    if (doubleClickTicks > 0) {
      if (--doubleClickTicks == 0) {
        button = Clicked;
      }
    }
  }
#endif // WITHOUT_BUTTON

}

// ----------------------------------------------------------------------------

int16_t ClickEncoder::getValue(void)
{
  int16_t val;

  cli();
  val = delta;

  if (steps == 2) delta = val & 1;
  else if (steps == 4) delta = val & 3;
  else delta = 0; // default to 1 step per notch

  sei();

  if (steps == 4) val >>= 2;
  if (steps == 2) val >>= 1;

  int16_t r = 0;
  int16_t accel = ((accelerationEnabled) ? (acceleration >> 8) : 0);

  if (val < 0) {
    r -= 1 + accel;
  }
  else if (val > 0) {
    r += 1 + accel;
  }

  return r;
}

// ----------------------------------------------------------------------------

#ifndef WITHOUT_BUTTON
ClickEncoder::Button ClickEncoder::getButton(void)
{
  ClickEncoder::Button ret = button;
  if (button != ClickEncoder::Held) {
    button = ClickEncoder::Open; // reset
  }
  return ret;
}

ClickEncoder::ButtonEvent ClickEncoder::getButtonEvent(void)
{
  ClickEncoder::ButtonEvent ret = event;
  event = ClickEncoder::NoEvent; // reset

  return ret;
}

bool ClickEncoder::isPressRotateLocked(void) const
{
  return pressRotateLocked;
}

uint8_t ClickEncoder::getPressRotateMultiplier(void) const
{
  if (!pressRotateLocked) {
    return 1;
  }
  return ENC_PRESSROTATE_MULTIPLIER + (pressRotateAcceleration >> 8);
}
#endif
