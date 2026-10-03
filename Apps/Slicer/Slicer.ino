#include <PicoPro.h>
#include <I2S.h>
#include <hardware/clocks.h>
#include <pico/time.h>
#include "SlicerAudio.h"
#include "SlicerUI.h"

static I2S audio(INPUT_PULLUP);
static SlicerAudio processor;
static bool controlsReady = false;
static bool audioReady = false; // Core1-only after setup1().
constexpr uint32_t kBitsPerSample = 32;
constexpr uint32_t kPIOClocksPerFrame = kBitsPerSample * 2u * 4u;

void setup() {
  PicoProSystemSettingsBegin();
  SlicerUI::begin();
  __atomic_store_n(&controlsReady, true, __ATOMIC_RELEASE);
  __sev();
}

void loop() {
  SlicerUI::service();
}

// Explicit event notification prevents losing a DMA wakeup just before WFE.
static void __not_in_flash_func(audioEvent)(void *) { __sev(); }

void setup1() {
  // Startup only: wait for routing, CV ownership and the first control packet.
  const absolute_time_t deadline = make_timeout_time_ms(2000);
  while (!__atomic_load_n(&controlsReady, __ATOMIC_ACQUIRE)) {
    if (best_effort_wfe_or_timeout(deadline)) return;
  }
  // Arduino-Pico's buffer lists use local IRQ masking, not cross-core locks.
  // begin(), its DMA IRQ and every read/write must therefore share Core1.
  audio.setDIN(I2S_DATAIN); audio.setDOUT(I2S_DATA);
  audio.setBCLK(BCLK); audio.setMCLK(MCLK); audio.setMCLKmult(256);
  audio.setBitsPerSample(kBitsPerSample); audio.setFrequency(SlicerEngine::kSampleRate);
  audio.setBuffers(4, 64);
  // Arduino-Pico 5.5.0 uses an integer PIO divider with MCLK enabled.
  // Preserve 250 MHz CPU but align DSP/IRQ timing to that actual frame rate.
  const uint32_t clockHz = clock_get_hz(clk_sys);
  const uint32_t divider = clockHz / (SlicerEngine::kSampleRate * kPIOClocksPerFrame);
  processor.begin(clockHz, divider * kPIOClocksPerFrame);
  audio.onReceive(audioEvent, nullptr); audio.onTransmit(audioEvent, nullptr);
  audioReady = audio.begin();
}

void loop1() {
  // Drain existing work before waiting. RX/TX IRQ callbacks latch an event,
  // including if they run between service() returning and this WFE.
  if (!audioReady || !processor.service(audio)) __wfe();
}
