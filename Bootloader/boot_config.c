// Copyright 2026 Wenhao Yang
//
// Author: Wenhao Yang
// Contributor: Wenhao Yang
//
// Global boot config loading and redundant flash-sector persistence.

#include "boot_config.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"

#define XIP_BASE_ADDR 0x10000000u

static uint32_t fnv1a32(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    hash ^= p[i];
    hash *= 16777619u;
  }
  return hash;
}

uint32_t pico_boot_config_crc(const PicoBootConfig *cfg) {
  return fnv1a32(cfg, offsetof(PicoBootConfig, crc32));
}

bool pico_boot_config_valid(const PicoBootConfig *cfg) {
  if (cfg->magic != PICO_BOOT_MAGIC) return false;
  if (cfg->version != PICO_BOOT_VERSION) return false;
  if (cfg->record_size != sizeof(PicoBootConfig)) return false;
  if (cfg->app_count == 0 || cfg->app_count > PICO_BOOT_MAX_APPS) return false;
  if (cfg->active_app >= cfg->app_count) return false;
  if (cfg->cpu_hz < 48000000u || cfg->cpu_hz > 300000000u) return false;
  if (cfg->crc32 != pico_boot_config_crc(cfg)) return false;
  return true;
}

void pico_boot_config_defaults(PicoBootConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->magic = PICO_BOOT_MAGIC;
  cfg->version = PICO_BOOT_VERSION;
  cfg->record_size = sizeof(PicoBootConfig);
  cfg->sequence = 1;
  cfg->active_app = 0;
  cfg->app_count = PICO_BOOT_DEFAULT_APP_COUNT;
  cfg->cpu_hz = PICO_BOOT_DEFAULT_CPU_HZ;
  cfg->calibration.cv1_counts_per_volt = PICO_BOOT_DEFAULT_CV1_COUNTS_PER_VOLT;
  cfg->calibration.cv2_counts_per_volt = PICO_BOOT_DEFAULT_CV2_COUNTS_PER_VOLT;
  cfg->calibration.cvout_counts_per_volt = PICO_BOOT_DEFAULT_CVOUT_COUNTS_PER_VOLT;
  cfg->calibration.cv1_zero_counts = PICO_BOOT_DEFAULT_CV1_ZERO_COUNTS;
  cfg->calibration.cv2_zero_counts = PICO_BOOT_DEFAULT_CV2_ZERO_COUNTS;
  cfg->calibration.cvout_zero_counts = 0.0f;

#if PICO_BOOT_TARGET_RP2350
  const uint32_t target_flag = PICO_BOOT_FLAG_RP2350;
#else
  const uint32_t target_flag = PICO_BOOT_FLAG_RP2040;
#endif

  for (uint8_t i = 0; i < cfg->app_count; ++i) {
    cfg->apps[i].flash_offset = PICO_BOOT_FIRST_APP_OFFSET +
                                ((uint32_t)i * PICO_BOOT_DEFAULT_SLOT_BYTES);
    cfg->apps[i].max_size = PICO_BOOT_DEFAULT_SLOT_BYTES;
    cfg->apps[i].flags = PICO_BOOT_FLAG_VALID | target_flag;
    cfg->apps[i].app_id = i + 1u;
    cfg->apps[i].vector_offset = PICO_BOOT_ARDUINO_VECTOR_OFFSET;
  }
  cfg->crc32 = pico_boot_config_crc(cfg);
}

static const PicoBootConfig *config_at(uint32_t flash_offset) {
  return (const PicoBootConfig *)(XIP_BASE_ADDR + flash_offset);
}

static bool sequence_newer_or_equal(uint32_t lhs, uint32_t rhs) {
  return (int32_t)(lhs - rhs) >= 0;
}

void pico_boot_config_load(PicoBootConfig *cfg) {
  const PicoBootConfig *a = config_at(PICO_BOOT_CONFIG_A_OFFSET);
  const PicoBootConfig *b = config_at(PICO_BOOT_CONFIG_B_OFFSET);
  const bool av = pico_boot_config_valid(a);
  const bool bv = pico_boot_config_valid(b);

  if (av && (!bv || sequence_newer_or_equal(a->sequence, b->sequence))) {
    memcpy(cfg, a, sizeof(*cfg));
  } else if (bv) {
    memcpy(cfg, b, sizeof(*cfg));
  } else {
    pico_boot_config_defaults(cfg);
  }
}

static void flash_write_sector(uint32_t flash_offset, const PicoBootConfig *cfg) {
  enum { CONFIG_PROGRAM_BYTES = 2 * FLASH_PAGE_SIZE };
  _Static_assert(sizeof(PicoBootConfig) <= CONFIG_PROGRAM_BYTES,
                 "boot config must fit in two flash pages");
  uint8_t pages[CONFIG_PROGRAM_BYTES];
  memset(pages, 0xff, sizeof(pages));
  memcpy(pages, cfg, sizeof(*cfg));

  const uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
  flash_range_program(flash_offset, pages, sizeof(pages));
  restore_interrupts(ints);
}

void pico_boot_config_save(PicoBootConfig *cfg) {
  cfg->magic = PICO_BOOT_MAGIC;
  cfg->version = PICO_BOOT_VERSION;
  cfg->record_size = sizeof(PicoBootConfig);
  cfg->sequence++;
  cfg->crc32 = 0;
  cfg->crc32 = pico_boot_config_crc(cfg);

  const PicoBootConfig *a = config_at(PICO_BOOT_CONFIG_A_OFFSET);
  const PicoBootConfig *b = config_at(PICO_BOOT_CONFIG_B_OFFSET);
  const bool av = pico_boot_config_valid(a);
  const bool bv = pico_boot_config_valid(b);
  const bool write_a = !av || (bv && sequence_newer_or_equal(b->sequence, a->sequence));
  flash_write_sector(write_a ? PICO_BOOT_CONFIG_A_OFFSET : PICO_BOOT_CONFIG_B_OFFSET, cfg);
}
