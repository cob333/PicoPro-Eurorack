
// Copyright 2026 Rich Heslip
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
// I/O pin definitions for Raspberry Pi Pico eurorack module


#ifndef IO_H_
#define IO_H_

#define TRUE 1
#define FALSE 0

#define PIN_WIRE_SDA 4
#define PIN_WIRE_SCL 5

// encoders 
#define ENCDIVIDE 2  // divide by 4 works best with my encoders
#define ENCA_IN 7 // ports we read values from MUX
#define ENCB_IN 6 // A & B swapped to get correct rotation
#define ENCSW_IN 8

// I2S pins for ADC and DAC
#define MCLK 11
#define BCLK 12
#define WS 13  // this will always be 1 pin above BCLK - can't change it
#define I2S_DATA 14  // Out of Pico to DAC
#define I2S_DATAIN 15  // into Pico from ADC

#define CPU_USE 9 // unused GPIO shows core 1 processor usage

// Gate/trigger digital inputs 
#define TRIGGER 26  // analog input works as a digital input as long as input voltage is 4v or more

#define AIN0 	26
#define AIN1 	27
#define AIN2 	28
#define AIN3 	29 // not available on standard Pico board

#define DISPLAY_BLANK_MS 60*1000  // display blanking time
#define OLED_DISPLAY   // for graphics conditionals

#define OLED_RESET -1        // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
#define SCREEN_WIDTH 64  // OLED display width, in pixels
#define SCREEN_HEIGHT 32  // OLED display height, in pixels
#define SCREEN_BUFFER_SIZE (SCREEN_WIDTH * ((SCREEN_HEIGHT + 7) / 8))


#endif // IO_H_

