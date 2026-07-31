// Copyright 2025 Rich Heslip
//
// Author: Rich Heslip 
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// misc defines and support routines for the PicoPro module
// R Heslip April 2026

#ifndef PICOPRO_H_
#define PICOPRO_H_

#include "PicoPro_io.h"
#include "BootSelector.h"
#include "ClickButton.h"
#include "CVPersistence.h"
#include "PicoBootConfig.h"
#include "scales.h"

// constants for integer to float and float to integer conversion
#define MULT_16 2147483647
#define DIV_16 4.6566129e-10


#define DEBOUNCE 10   // debounce for buttons
#define GATE_DEBOUNCE 1  // some modules output a very short trigger so don't do a long debounce
#define TRIG_DEBOUNCE 1  // short trigger debounce too
#define CLOCK_DEBOUNCE 1 // short clock debounce
#define PARAMETERUPDATE 100  // some DaisySP models don't like values that jump around a lot so limit the changes


// A/D values from CV inputs

#define CV1IN AIN0   // CV1 input - middle left jack
#define CV2IN AIN1   // CV2 input - middle right jack
#define CV3IN AIN2   // CV3 input - top left jack
#define CV4IN AIN3   // CV4 input - top right jack
#define CV_BURST_SAMPLES 12

// a/d values from pots
// the pots are "locked" when the parameter page changes
// this prevents an immediate change when we switch pages
// we unlock each pot and allow parameter values to change when there is a significant movement of the pot

#define AD_BITS 12
#define AD_RANGE 4096  // RP2350 has 12 bit A/D so lets use it

static inline uint16_t sampleCVInput(uint8_t pin) {
  uint32_t sum = 0;
  uint16_t lowest = UINT16_MAX;
  uint16_t highest = 0;
  for (uint8_t i = 0; i < CV_BURST_SAMPLES; ++i) {
    const uint16_t sample = analogRead(pin);
    sum += sample;
    if (sample < lowest) lowest = sample;
    if (sample > highest) highest = sample;
  }

  // A trimmed mean suppresses both isolated converter spikes and ordinary ADC
  // noise without carrying state between calls, so voltage steps are not
  // turned into portamento.
  sum -= lowest;
  sum -= highest;
  return (uint16_t)((sum + ((CV_BURST_SAMPLES - 2) / 2)) /
                    (CV_BURST_SAMPLES - 2));
}

// sample the CV1 input. 
static inline uint16_t sampleCV1(void) {
  return sampleCVInput(CV1IN);
}

// sample the CV2 input. 
static inline uint16_t sampleCV2(void) {
  return sampleCVInput(CV2IN);
}

// sample the CV3 input. 
static inline uint16_t sampleCV3(void) {
  return sampleCVInput(CV3IN);
}

// sample the CV4 input. 
static inline uint16_t sampleCV4(void) {
  return sampleCVInput(CV4IN);
}

// timer stuff - periodic timer used to service encoder
#define TIMER_MICROS 1000 // interrupt period
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

// like the map() function but maps integer to float values
static inline float mapf(long x, long in_min, long in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#endif // PICOPRO_H_
