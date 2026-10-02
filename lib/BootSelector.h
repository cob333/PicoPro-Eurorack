#ifndef PICOPRO_BOOT_SELECTOR_H_
#define PICOPRO_BOOT_SELECTOR_H_

#include <Arduino.h>
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "pico/platform.h"
#include "pico/multicore.h"
#include "ClickEncoder.h"

#ifndef PICOPRO_SELECTOR_EXIT_HOLD_MS
#define PICOPRO_SELECTOR_EXIT_HOLD_MS 1800u
#endif

#ifndef PICOPRO_SELECTOR_EXIT_WATCHDOG_MS
#define PICOPRO_SELECTOR_EXIT_WATCHDOG_MS 2000u
#endif

#define PICOPRO_SELECTOR_RETURN_MAGIC 0x50435254u

typedef void (*PicoProSelectorExitCallback)(void);

static inline void PicoProServiceSelectorExitImpl(
    uint8_t button_pin,
    bool block_long_press,
    PicoProSelectorExitCallback before_exit = nullptr,
    PicoProSelectorExitCallback prepare_exit = nullptr) {
  static uint32_t pressed_at = 0;
  static bool exiting = false;

  if (exiting) {
    return;
  }

  if (block_long_press) {
    pressed_at = 0;
    return;
  }

  if (digitalRead(button_pin) == LOW) {
    if (pressed_at == 0) {
      pressed_at = millis();
    } else if ((millis() - pressed_at) >= PICOPRO_SELECTOR_EXIT_HOLD_MS) {
      exiting = true;
      watchdog_hw->scratch[0] = PICOPRO_SELECTOR_RETURN_MAGIC;
      watchdog_reboot(0, 0, PICOPRO_SELECTOR_EXIT_WATCHDOG_MS);
      if (prepare_exit != nullptr) {
        prepare_exit();
      }
      // Flash persistence uses the Arduino-Pico core lockout handshake. Save
      // while core1 is still alive; resetting it first makes idleOtherCore()
      // wait for a core that can no longer acknowledge the request.
      if (before_exit != nullptr) {
        before_exit();
      }
      if (get_core_num() == 0) {
        multicore_reset_core1();
        rp2040.fifo.begin(1);
      }
      noInterrupts();
      watchdog_reboot(0, 0, 10);
      while (true) {
      }
    }
  } else {
    pressed_at = 0;
  }
}

static inline void PicoProServiceSelectorExit(
    uint8_t button_pin,
    PicoProSelectorExitCallback before_exit = nullptr,
    PicoProSelectorExitCallback prepare_exit = nullptr) {
  PicoProServiceSelectorExitImpl(button_pin, false, before_exit, prepare_exit);
}

static inline void PicoProServiceSelectorExit(
    uint8_t button_pin,
    ClickEncoder &encoder,
    PicoProSelectorExitCallback before_exit = nullptr,
    PicoProSelectorExitCallback prepare_exit = nullptr) {
  PicoProServiceSelectorExitImpl(
      button_pin,
      encoder.isPressRotateLocked(),
      before_exit,
      prepare_exit);
}

// Backward-compatible aliases for sketches built against the former 4HPico
// API. New PicoPro code should use PicoProServiceSelectorExit().
static inline void FourHPicoServiceSelectorExit(
    uint8_t button_pin,
    PicoProSelectorExitCallback before_exit = nullptr,
    PicoProSelectorExitCallback prepare_exit = nullptr) {
  PicoProServiceSelectorExit(button_pin, before_exit, prepare_exit);
}

static inline void FourHPicoServiceSelectorExit(
    uint8_t button_pin,
    ClickEncoder &encoder,
    PicoProSelectorExitCallback before_exit = nullptr,
    PicoProSelectorExitCallback prepare_exit = nullptr) {
  PicoProServiceSelectorExit(button_pin, encoder, before_exit, prepare_exit);
}

#endif // PICOPRO_BOOT_SELECTOR_H_
