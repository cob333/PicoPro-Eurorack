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
R Heslip Delay for PicoPro March 2026

// Stereo Delay using the Pico-Audio framework which is a port of PJRC's Teensy Audio
// runs OK at 150 Mhz - very low CPU usage

Top Jacks - audio inputs

Middle jacks - 

Bottom Jacks - audio outputs

Menu Parameters:

  Delay MS - delay time (both channels) in milliseconds
  Fdback - % feedback from delay back to input for repeats
  X Fdback - % cross feedback from delay back to other input for stereo ping pong effect
  Mix - dry/wet mix
  Level - signal output level

*/

#include "PicoPro.h"
#include <I2S.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <pico-audio.h>
#include "ClickEncoder.h"
#include "PicoAppState.h"

#define DEBUG   // comment out to remove debug code

enum UISTATES {RUN,DORMANT,WAIT_BUTTON_RELEASE};
int16_t UI_state=RUN; // initial UI state

#define DEBOUNCE_CYCLES 100 // counter to debounce button release

int32_t displaytimer; // display blanking timer

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

// Pico Audio lib patch setup
// sample rate is fixed at 48k in the Pico Audio library
// you can change it by editing the definitions in AudioStream.h
// BE AWARE - this sketch uses almost all of the Pico 2 internal RAM. increasing sample rate will chew up proportionately more memory

AudioInputI2S            i2s1;           //xy=155,100
AudioOutputI2S           i2s2;           //xy=1132,85

AudioEffectDelay         delay1;         //xy=500,266
AudioMixer4              mixer1;         //xy=500,84
AudioEffectDelay         delay2;         //xy=500,266
AudioMixer4              mixer2;         //xy=500,84
AudioMixer4              mixeroutL;         //xy=500,84
AudioMixer4              mixeroutR;         //xy=500,84
AudioConnection     patchCord1(i2s1, 0, mixer1, 0);
AudioConnection     patchCord2(mixer1, delay1);
AudioConnection     patchCord3(i2s1, 1, mixer2, 0);
AudioConnection     patchCord4(mixer2, delay2);
AudioConnection     patchCord6(delay1, 0, mixer1, 1); // delay feedback
AudioConnection     patchCord7(delay2, 0, mixer2, 1); // delay feedback
AudioConnection     patchCord8(delay1, 0, mixer2, 2); // cross feedback
AudioConnection     patchCord9(delay2, 0, mixer1, 2); // cross feedback
AudioConnection     patchCord10(mixer1, 0, mixeroutL, 1);
AudioConnection     patchCord11(mixer2, 0, mixeroutR, 1);
AudioConnection     patchCord12(i2s1, 0, mixeroutL, 0);  // dry signal output mixer
AudioConnection     patchCord13(i2s1, 1, mixeroutR, 0);
AudioConnection     patchCord14(mixeroutL, 0, i2s2, 0);
AudioConnection     patchCord15(mixeroutR, 0, i2s2, 1);

int16_t delaytime0 = 500;
int16_t delayfeedback = 100;
int16_t crossfeedback = 100;
int16_t wetdrymix = 100;
int16_t outputlevel = 800;

#define DELAY_STATE_TAG PICOPRO_APP_STATE_TAG('D', 'L', 'Y', '2')
#define DELAY_LEGACY_STATE_TAG PICOPRO_APP_STATE_TAG('D', 'L', 'Y', '1')

struct DelayState {
  int16_t delaytime0;
  int16_t delayfeedback;
  int16_t crossfeedback;
  int16_t wetdrymix;
  int16_t outputlevel;
  PicoCVPersistentState cv;
};

struct DelayLegacyState {
  int16_t delaytime0;
  int16_t delayfeedback;
  int16_t crossfeedback;
  int16_t wetdrymix;
  int16_t outputlevel;
};

void loadDelayState() {
  DelayState state;
  if (PicoAppStateLoad(DELAY_STATE_TAG, &state, sizeof(state))) {
    delaytime0 = state.delaytime0;
    delayfeedback = state.delayfeedback;
    crossfeedback = state.crossfeedback;
    wetdrymix = state.wetdrymix;
    outputlevel = state.outputlevel;
    PicoCVImportState(&state.cv);
    return;
  }
  DelayLegacyState legacy;
  if (PicoAppStateLoad(DELAY_LEGACY_STATE_TAG, &legacy, sizeof(legacy))) {
    delaytime0 = legacy.delaytime0;
    delayfeedback = legacy.delayfeedback;
    crossfeedback = legacy.crossfeedback;
    wetdrymix = legacy.wetdrymix;
    outputlevel = legacy.outputlevel;
  }
}

void saveDelayState() {
  DelayState state = {
    delaytime0,
    delayfeedback,
    crossfeedback,
    wetdrymix,
    outputlevel,
  };
  PicoCVExportState(&state.cv);
  PicoAppStateSave(DELAY_STATE_TAG, &state, sizeof(state));
}

void prepareDelayExit() {
  mixeroutL.gain(0, 0.0f);
  mixeroutL.gain(1, 0.0f);
  mixeroutR.gain(0, 0.0f);
  mixeroutR.gain(1, 0.0f);
  delay(32);
}

// menu callback to update all settings
// parameters passed by menusystem not used because we are updating everything on every change
void applyDelayPatch(int16_t delay_ms,
                     int16_t feedback,
                     int16_t x_feedback,
                     int16_t mix,
                     int16_t level) {
    // delay tap connects through a
  // mixer to the right channel output

  delay1.delay(0, delay_ms);
  delay2.delay(0, delay_ms);
  mixer1.gain(0, 0.8);  // dry input signal
  mixer1.gain(1,(float)feedback/1000);  // delayed signal feedback
  mixer2.gain(0, 0.8);  // dry input signal
  mixer2.gain(1,(float)feedback/1000);  // delayed signal feedback
  mixer1.gain(2,(float)x_feedback/1000);  // ping pong feedback
  mixer2.gain(2,(float)x_feedback/1000);  //  ""
  mixeroutL.gain(1,(float)mix/1000*(float)level/1000); // wet signal gain
  mixeroutR.gain(1,(float)mix/1000*(float)level/1000);
  mixeroutL.gain(0,(1.0f-(float)mix/1000)*(float)level/1000); // dry signal gain
  mixeroutR.gain(0,(1.0f-(float)mix/1000)*(float)level/1000);
}

void updatepatch(int16_t dummy1, int16_t dummy2) {
  applyDelayPatch(delaytime0, delayfeedback, crossfeedback, wetdrymix, outputlevel);
}

#include "Delay_Menu.h"
#include "MenuSystem.h"

void serviceDelayCV() {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if ((now - last_ms) < 20) {
    return;
  }
  last_ms = now;
  applyDelayPatch(
      PicoCVModulatedValue(0, delaytime0, menus[0].min, menus[0].max),
      PicoCVModulatedValue(1, delayfeedback, menus[1].min, menus[1].max),
      PicoCVModulatedValue(2, crossfeedback, menus[2].min, menus[2].max),
      PicoCVModulatedValue(3, wetdrymix, menus[3].min, menus[3].max),
      PicoCVModulatedValue(4, outputlevel, menus[4].min, menus[4].max));
}

void setup() {

  Serial.begin(115200);
// init IO ports
  pinMode(ENCA_IN, INPUT_PULLUP);  // menu encoder and switch
  pinMode(ENCB_IN, INPUT_PULLUP);    
  pinMode(ENCSW_IN, INPUT_PULLUP); 

// set up I2C pins 
  Wire.setSDA(PIN_WIRE_SDA);
  Wire.setSCL(PIN_WIRE_SCL);
  Wire.begin();

// set up timer interrupt 
  alarm_in_us(TIMER_MICROS);

  PicoCVInputBegin();

  // give the audio library some memory.  We'll be able
  // to see how much it actually uses, which can be used
  // to reduce this to the minimum necessary.
  AudioMemory(1000);  // determined by watching the audio stats when delay is at max
  PicoCVBindMenus(menus, NUM_MENUS);
  loadDelayState();
  updatepatch(0,0); // set initial values

	i2s2.begin(BCLK,WS,MCLK,I2S_DATAIN,I2S_DATA); // set up I2S for PCM5102A and PCM1808 which requires MCLK and a data input pin

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


int count = 0;
int speed = 60;


// first core handles Pico Audio processing under interrupts
void loop() {

  PicoProServiceSelectorExit(ENCSW_IN, menuenc, saveDelayState, prepareDelayExit);
  serviceDelayCV();

  // print a summary of the current & maximum usage

  Serial.print("all=");
  Serial.print(AudioProcessorUsage());
  Serial.print(",");
  Serial.print(AudioProcessorUsageMax());
  Serial.print("    ");
  Serial.print("Memory: ");
  Serial.print(AudioMemoryUsage());
  Serial.print(",");
  Serial.print(AudioMemoryUsageMax());
  Serial.print("    ");

  Serial.println();
  delay(speed);

}

// second core setup
// second core is dedicated to UI
void setup1() {
delay (1000); // wait for main core to start up peripherals
}

void loop1() {
  static int16_t debouncecounter;
  int16_t encvalue;

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
