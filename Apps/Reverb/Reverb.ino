
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
/*
Reverb example for PicoPro hardware 
R Heslip  April 2026

Uses most of the Pico2's memory but its reasonably light on CPU - can run 44khz sampling at 150mhz

Top Jacks - left and right Audio inputs 

Bottom Jacks - left and right  Audio out

Menu Parameters

Time - Reverb Time

Filter - Reverb Filter cutoff

Mix - Blends reverb signal with dry signal

Level - Output level

*/

#include "PicoPro.h"
#include <I2S.h>
#include <math.h>
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "ClickEncoder.h"
#include "pico/multicore.h"
#include "PicoAppState.h"

#define DEBUG   // comment out to remove debug code
#define MONITOR_CPU1  // define to enable 2nd core monitoring

#define SAMPLERATE 44100


I2S i2s(INPUT_PULLUP); // both input and output

#include "daisysp.h"

// including the source files is a pain but that way you compile in only the modules you need
// DaisySP statically allocates memory and some modules e.g. reverb use a lot of ram
#include "effects/reverbsc.cpp"

float samplerate=SAMPLERATE;  // for DaisySP
daisysp::ReverbSc reverb;

enum UISTATES {RUN,DORMANT,WAIT_BUTTON_RELEASE};
int16_t UI_state=RUN; // initial UI state

#define DEBOUNCE_CYCLES 100 // counter to debounce button release

int32_t displaytimer ; // display blanking timer

ClickEncoder menuenc(ENCA_IN,ENCB_IN,ENCSW_IN,ENCDIVIDE); // menu encoder object

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#include "ui/CVModulation.h"

// timer routines
static void alarm_in_us(uint32_t delay_us) {
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
  irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
  irq_set_enabled(ALARM_IRQ, true);
  alarm_in_us_arm(delay_us);
}

static void alarm_in_us_arm(uint32_t delay_us) {
  uint64_t target = timer_hw->timerawl + delay_us;
  timer_hw->alarm[ALARM_NUM] = (uint32_t) target;
}

// timer interrupt handler
//handles the menu encoder
static void alarm_irq(void) {
  menuenc.service(); // handle the menu encoder
  hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM); // clear IRQ flag
  alarm_in_us_arm(TIMER_MICROS);  // reschedule interrupt
}

// menu parameters are always 16 bit ints to simplify the menu system
// they must be converted to other types as needed in the sketch
int16_t reverbfeedback=50;  // set initial values
int16_t reverblpf=5050;
int16_t reverbmix=50;
int16_t reverblevel=75;
static volatile int16_t active_reverbmix = 50;
static volatile int16_t active_reverblevel = 75;
static volatile int16_t reverb_exit_level = 100;

#define REVERB_STATE_TAG PICOPRO_APP_STATE_TAG('R', 'V', 'B', '2')
#define REVERB_LEGACY_STATE_TAG PICOPRO_APP_STATE_TAG('R', 'V', 'B', '1')

struct ReverbState {
  int16_t reverbfeedback;
  int16_t reverblpf;
  int16_t reverbmix;
  int16_t reverblevel;
  PicoCVPersistentState cv;
};

struct ReverbLegacyState {
  int16_t reverbfeedback;
  int16_t reverblpf;
  int16_t reverbmix;
  int16_t reverblevel;
};

void loadReverbState() {
  ReverbState state;
  if (PicoAppStateLoad(REVERB_STATE_TAG, &state, sizeof(state))) {
    reverbfeedback = state.reverbfeedback;
    reverblpf = state.reverblpf;
    reverbmix = state.reverbmix;
    reverblevel = state.reverblevel;
    PicoCVImportState(&state.cv);
    return;
  }
  ReverbLegacyState legacy;
  if (PicoAppStateLoad(REVERB_LEGACY_STATE_TAG, &legacy, sizeof(legacy))) {
    reverbfeedback = legacy.reverbfeedback;
    reverblpf = legacy.reverblpf;
    reverbmix = legacy.reverbmix;
    reverblevel = legacy.reverblevel;
  }
}

void saveReverbState() {
  ReverbState state = {
    reverbfeedback,
    reverblpf,
    reverbmix,
    reverblevel,
  };
  PicoCVExportState(&state.cv);
  PicoAppStateSave(REVERB_STATE_TAG, &state, sizeof(state));
}

void prepareReverbExit() {
  reverb_exit_level = 0;
  delay(32);
}

// menu callback functions
void applyReverbFeedback(int16_t value) {
  reverb.SetFeedback(mapf(value,0,100,0,0.96)); // 100% feedback distorts
}

void applyReverbLpf(int16_t value) {
  reverb.SetLpFreq((float)value);
}

void setreverbfeedback(void) {
  applyReverbFeedback(reverbfeedback);
}

void setreverblpf(void) {
  applyReverbLpf(reverblpf);
}

#include "Reverb_Menu.h"
#include "MenuSystem.h"  // has to come after display and encoder objects creation

void serviceReverbCV() {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if ((now - last_ms) < 20) {
    return;
  }
  last_ms = now;

  const int16_t feedback = PicoCVModulatedValue(0, reverbfeedback, menus[0].min, menus[0].max);
  const int16_t lpf = PicoCVModulatedValue(1, reverblpf, menus[1].min, menus[1].max);
  active_reverbmix = PicoCVModulatedValue(2, reverbmix, menus[2].min, menus[2].max);
  active_reverblevel = PicoCVModulatedValue(3, reverblevel, menus[3].min, menus[3].max);
  applyReverbFeedback(feedback);
  applyReverbLpf(lpf);
}



void setup() { 
  Serial.begin(115200);

// init IO ports
  pinMode(ENCA_IN, INPUT_PULLUP);  // menu encoder and switch
  pinMode(ENCB_IN, INPUT_PULLUP);    
  pinMode(ENCSW_IN, INPUT_PULLUP); 
#ifdef MONITOR_CPU1 // for monitoring 2nd core CPU usage
  pinMode(CPU_USE,OUTPUT); // hi = CPU busy
#endif 

// set up I2C pins 
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();

// set up timer interrupt 
  alarm_in_us(TIMER_MICROS);

  analogReadResolution(AD_BITS); // set up for max resolution

// set up I2S for 32 bits in and out
// PCM1808 is 24 bit only but I could not get 24 bit I2S working. 32 bits is little if any extra overhead
  i2s.setDOUT(I2S_DATA);
  i2s.setDIN(I2S_DATAIN);
  i2s.setBCLK(BCLK); // Note: LRCLK = BCLK + 1
  i2s.setMCLK(MCLK);
  i2s.setMCLKmult(256);
  i2s.setBitsPerSample(32);
  // Keep the hardware frame rate and the DaisySP time base identical.
  i2s.setFrequency(SAMPLERATE);
  i2s.begin();

  reverb.Init(samplerate);
  PicoCVBindMenus(menus, NUM_MENUS);
  loadReverbState();
  active_reverbmix = reverbmix;
  active_reverblevel = reverblevel;
  setreverbfeedback(); // initial settings
  setreverblpf();

  // set up OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while(1); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setFont(NULL);
  display.setTextSize(1);

	display.setTextColor(WHITE,BLACK); // foreground, background  
  displaytimer=millis(); // reset display blanking timer
  drawmenu(0); // show first menu item
  menuenc.getValue(); // clear any initial input
}


// first Pico core does UI etc - not super time critical
void loop() {
  static int16_t debouncecounter;
  int16_t encvalue;

  PicoProServiceSelectorExit(ENCSW_IN, menuenc, saveReverbState, prepareReverbExit);
  serviceReverbCV();

  if ((millis()-displaytimer) > DISPLAY_BLANK_MS) {
    UI_state=DORMANT;  // 
    blankdisplay(); // protect the OLED from burnin
  } 

  switch (UI_state) {
    case RUN:
     domenus();  // call the menu state machine
   //  encvalue=menuenc.getValue();
   //  if (encvalue) Serial.printf("%d\n",encvalue);
      break;    
    case DORMANT:  // using menu encoder will start screen up again
      encvalue=menuenc.getValue();
      if (encvalue || !digitalRead(ENCSW_IN)) {
        updatedisplay(); // restore the display
        debouncecounter=DEBOUNCE_CYCLES;
        UI_state=WAIT_BUTTON_RELEASE;
      }
      break; 
    case WAIT_BUTTON_RELEASE:  // intermediate state - wait for button release so it doesn't mess up menus
      if (digitalRead(ENCSW_IN)) {
        -- debouncecounter;
        if (debouncecounter==0) UI_state=RUN;  // if button released move to run state
      }
      break;
    default:
      UI_state=RUN;
      break;

  }
}


// second core setup
// second core is dedicated to sample processing
void setup1() {
delay (1000); // wait for main core to start up peripherals
}

// process audio samples
void loop1(){
  float sigL,sigR,outL,outR,mix,level;
  int32_t left,right;

// these calls will stall if not data is available
  left=i2s.read();    // input is mono but we still have to read both channels
  right=i2s.read();

#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE,1); // hi = CPU busy
#endif

  sigL=left*DIV_16; // convert input to float for DaisySP
  sigR=right*DIV_16; 

  mix=(float)(active_reverbmix)/100; // convert menu values to float
  level=(float)(active_reverblevel)/100;

  reverb.Process(sigL, sigR, &outL, &outR); 

 // sigL=(sigL*(1-mix)+ outL*mix)*level; // full dry to full wet signal mix
//  sigR=(sigR*(1-mix)+ outR*mix)*level;

  sigL=(sigL+ outL*mix)*level; // add reverb to dry signal - I think this sounds better
  sigR=(sigR+ outR*mix)*level;

  static uint8_t exit_ramp_div = 0;
  static int16_t exit_level = 100;
  if (++exit_ramp_div >= 16) {
    exit_ramp_div = 0;
    if (exit_level > reverb_exit_level) {
      --exit_level;
    } else if (exit_level < reverb_exit_level) {
      ++exit_level;
    }
  }
  sigL *= (float)exit_level / 100.0f;
  sigR *= (float)exit_level / 100.0f;

  left=(int32_t)(sigL*MULT_16); // convert output back to int32
  right=(int32_t)(sigR*MULT_16); // convert output back to int32

#ifdef MONITOR_CPU1  
  digitalWrite(CPU_USE,0); // low - CPU not busy
#endif
// these calls will stall if buffer is full
	i2s.write(left); // left passthru
	i2s.write(right); // right passthru

}
