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
// changed to just handle buttons R Heslip 12/2025
// ----------------------------------------------------------------------------

#ifndef __have__ClickButton_h__
#define __have__ClickButton_h__

// ----------------------------------------------------------------------------

//#include <stdint.h>
//#include <avr/io.h>
//#include <avr/interrupt.h>
//#include <avr/pgmspace.h>
#include "Arduino.h"


// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Button configuration (values for 1ms timer service calls)
//
#define ENC_BUTTONINTERVAL    10  // check button every x milliseconds, also debouce time
#define ENC_DOUBLECLICKTIME  800  // second click within 00ms
#define ENC_HOLDTIME        500  // report held button after .5s

class ClickButton
{
public:
  typedef enum Button_e {
    Open = 0,
    Closed,

    Pressed,
    Held,
    Released,

    Clicked,
    DoubleClicked

  } Button;

public:
  ClickButton(uint8_t BTN = -1,
               bool active = LOW);

  void service(void);

public:
  Button getButton(void);

public:
  void setDoubleClickEnabled(const bool &d)
  {
    doubleClickEnabled = d;
  }

  const bool getDoubleClickEnabled()
  {
    return doubleClickEnabled;
  }


private:

  const uint8_t pinBTN;
  const bool pinsActive;

  volatile Button button;
  bool doubleClickEnabled;
  uint16_t keyDownTicks = 0;
  uint8_t doubleClickTicks = 0;
  unsigned long lastButtonCheck = 0;

};



// ----------------------------------------------------------------------------

#endif // __have__ClickButton_h__

