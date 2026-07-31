// Copyright 2026 Wenhao Yang
//
// Author: Wenhao Yang
// Contributor: Wenhao Yang
//
// Bootloader flash layout and shared global config records for Pico-Eurorack.

#ifndef PICO_BOOT_CONFIG_H_
#define PICO_BOOT_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

#define PICO_BOOT_MAGIC 0x50494253u
#define PICO_BOOT_VERSION 5u
#define PICO_BOOT_MAX_APPS 20u
#define PICO_BOOT_DEFAULT_APP_COUNT 20u

#define PICO_BOOT_BYTES (256u * 1024u)
#define PICO_BOOT_CONFIG_A_OFFSET (PICO_BOOT_BYTES - (2u * 4096u))
#define PICO_BOOT_CONFIG_B_OFFSET (PICO_BOOT_BYTES - 4096u)
#define PICO_BOOT_FIRST_APP_OFFSET PICO_BOOT_BYTES

#define PICO_BOOT_DEFAULT_SLOT_BYTES (512u * 1024u)
#define PICO_BOOT_ARDUINO_VECTOR_OFFSET 0x3000u

#define PICO_BOOT_FLAG_VALID 0x01u
#define PICO_BOOT_FLAG_RP2040 0x02u
#define PICO_BOOT_FLAG_RP2350 0x04u
#define PICO_BOOT_FLAG_PICOFX 0x08u

#define PICO_BOOT_DEFAULT_CV1_COUNTS_PER_VOLT 583.60f
#define PICO_BOOT_DEFAULT_CV2_COUNTS_PER_VOLT 587.00f
#define PICO_BOOT_DEFAULT_CV1_ZERO_COUNTS 3244.90f
#define PICO_BOOT_DEFAULT_CV2_ZERO_COUNTS 3261.00f
#define PICO_BOOT_DEFAULT_CV_COUNTS_PER_VOLT PICO_BOOT_DEFAULT_CV1_COUNTS_PER_VOLT
#define PICO_BOOT_DEFAULT_CV_ZERO_COUNTS PICO_BOOT_DEFAULT_CV1_ZERO_COUNTS
#define PICO_BOOT_DEFAULT_CVOUT_COUNTS_PER_VOLT 5456.0f
#define PICO_BOOT_DEFAULT_CPU_HZ 250000000u

typedef struct {
  uint32_t flash_offset;
  uint32_t max_size;
  uint32_t flags;
  uint32_t app_id;
  uint32_t vector_offset;
} PicoBootAppSlot;

typedef struct {
  float cv1_counts_per_volt;
  float cv2_counts_per_volt;
  float cvout_counts_per_volt;
  float cv1_zero_counts;
  float cv2_zero_counts;
  float cvout_zero_counts;
  float reserved[2];
} PicoBootCalibration;

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t record_size;
  uint32_t sequence;
  uint8_t active_app;
  uint8_t app_count;
  uint16_t reserved0;
  uint32_t cpu_hz;
  PicoBootCalibration calibration;
  PicoBootAppSlot apps[PICO_BOOT_MAX_APPS];
  uint32_t crc32;
} PicoBootConfig;

uint32_t pico_boot_config_crc(const PicoBootConfig *cfg);
bool pico_boot_config_valid(const PicoBootConfig *cfg);
void pico_boot_config_defaults(PicoBootConfig *cfg);
void pico_boot_config_load(PicoBootConfig *cfg);
void pico_boot_config_save(PicoBootConfig *cfg);

#endif
