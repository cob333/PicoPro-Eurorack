#include <PicoPro.h>
#include <I2S.h>
#include <hardware/clocks.h>
#include "SlicerAudio.h"
#include "SlicerUI.h"

static I2S audio(INPUT_PULLUP);
static SlicerAudio processor;
static bool audioReady = false;
constexpr uint32_t kBitsPerSample = 32;
constexpr uint32_t kPIOClocksPerFrame = kBitsPerSample * 2u * 4u;

void setup() {
  PicoProSystemSettingsBegin();
  SlicerUI::begin();
  audio.setDIN(I2S_DATAIN); audio.setDOUT(I2S_DATA);
  audio.setBCLK(BCLK); audio.setMCLK(MCLK); audio.setMCLKmult(256);
  audio.setBitsPerSample(kBitsPerSample); audio.setFrequency(SlicerEngine::kSampleRate);
  audio.setBuffers(4, 64);
  // Arduino-Pico 5.5.0 uses an integer PIO divider with MCLK enabled.
  // Preserve 250 MHz CPU but align DSP/IRQ timing to that actual frame rate.
  const uint32_t clockHz = clock_get_hz(clk_sys);
  const uint32_t divider = clockHz / (SlicerEngine::kSampleRate * kPIOClocksPerFrame);
  processor.begin(clockHz, divider * kPIOClocksPerFrame);
  __atomic_store_n(&audioReady, audio.begin(), __ATOMIC_RELEASE);
}

void loop() {
  SlicerUI::service();
}

void setup1() {}
void loop1() {
  if (__atomic_load_n(&audioReady, __ATOMIC_ACQUIRE)) processor.service(audio);
}
