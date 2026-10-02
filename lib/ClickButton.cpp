// ----------------------------------------------------------------------------
// Button driver based on ClickEncoder
// Supports Click, DoubleClick, Long Click
//
// (c) 2010 karl@pitrich.com
// (c) 2014 karl@pitrich.com
//
// Timer-based logic by Peter Dannegger
// http://www.mikrocontroller.net/articles/Drehgeber
//
// changed to just handle buttons R Heslup 12/2025
// ----------------------------------------------------------------------------

#include "ClickButton.h"

// ----------------------------------------------------------------------------
// Button configuration (values for 1ms timer service calls)
//
#define ENC_BUTTONINTERVAL    10  // check button every x milliseconds, also debouce time
#define ENC_DOUBLECLICKTIME  800  // second click within 00ms
#define ENC_HOLDTIME        500  // report held button after .5s



// ----------------------------------------------------------------------------

ClickButton::ClickButton(uint8_t BTN, bool active)
  : doubleClickEnabled(true), 
    button(Open), 
    pinBTN(BTN), pinsActive(active)
{
  uint8_t configType = (pinsActive == LOW) ? INPUT_PULLUP : INPUT;
  pinMode(pinBTN, configType);

}

// ----------------------------------------------------------------------------
// call this every 1 millisecond via timer ISR
//
void ClickButton::service(void)
{
  unsigned long now = millis();


 // handle button
  //

  if (pinBTN > 0 // check button only, if a pin has been provided
      && (now - lastButtonCheck) >= ENC_BUTTONINTERVAL) // checking button is sufficient every 10-30ms
  {
    lastButtonCheck = now;

    if (digitalRead(pinBTN) == pinsActive) { // key is down
      button=Closed;
      keyDownTicks++;
      if (keyDownTicks > (ENC_HOLDTIME / ENC_BUTTONINTERVAL)) {
        button = Held;
      }
    }

    if (digitalRead(pinBTN) == !pinsActive) { // key is now up
      if (keyDownTicks /*> ENC_BUTTONINTERVAL*/) {
        if (button == Held) {
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

      keyDownTicks = 0;
    }

    if (doubleClickTicks > 0) {
      doubleClickTicks--;
      if (--doubleClickTicks == 0) {
        button = Clicked;
      }
    }
  }

}

// ----------------------------------------------------------------------------

ClickButton::Button ClickButton::getButton(void)
{
  ClickButton::Button ret = button;
  if (button != ClickButton::Held) {
    button = ClickButton::Open; // reset
  }
  return ret;
}


