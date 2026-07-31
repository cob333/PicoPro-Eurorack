#ifndef PICOPRO_APP_STATE_H_
#define PICOPRO_APP_STATE_H_

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hardware/flash.h"
#include "PicoBootConfig.h"

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (4u * 1024u * 1024u)
#endif

#define PICOPRO_APP_STATE_MAGIC 0x50415053u
#define PICOPRO_APP_STATE_VERSION 1u
#define PICOPRO_APP_STATE_BYTES (160u * 1024u)
#define PICOPRO_APP_STATE_SLOT_BYTES (8u * 1024u)
#define PICOPRO_APP_STATE_LEGACY_SLOTS 8u
#define PICOPRO_APP_STATE_FLASH_OFFSET \
  (PICO_FLASH_SIZE_BYTES - (PICOPRO_APP_STATE_LEGACY_SLOTS * PICOPRO_APP_STATE_SLOT_BYTES))
#define PICOPRO_APP_STATE_PAYLOAD_BYTES 232u
#define PICOPRO_APP_STATE_TAG(a, b, c, d) \
  (((uint32_t)(uint8_t)(a)) | ((uint32_t)(uint8_t)(b) << 8) | \
   ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t payload_size;
  uint32_t app_id;
  uint32_t schema_tag;
  uint32_t sequence;
  uint32_t crc32;
  uint8_t payload[PICOPRO_APP_STATE_PAYLOAD_BYTES];
} PicoAppStateRecord;

static_assert(sizeof(PicoAppStateRecord) == FLASH_PAGE_SIZE,
              "Pico app state record must fit exactly in one flash page");

static inline uint32_t PicoAppStateCrc(const PicoAppStateRecord *record) {
  PicoAppStateRecord tmp;
  memcpy(&tmp, record, sizeof(tmp));
  tmp.crc32 = 0;
  return PicoBootFnv1a32(&tmp, sizeof(tmp));
}

static inline int PicoAppStateRecordValid(const PicoAppStateRecord *record,
                                          uint32_t app_id,
                                          uint32_t schema_tag,
                                          uint16_t payload_size) {
  if (record->magic != PICOPRO_APP_STATE_MAGIC) return 0;
  if (record->version != PICOPRO_APP_STATE_VERSION) return 0;
  if (record->payload_size != payload_size) return 0;
  if (record->payload_size > PICOPRO_APP_STATE_PAYLOAD_BYTES) return 0;
  if (record->app_id != app_id) return 0;
  if (record->schema_tag != schema_tag) return 0;
  if (record->crc32 != PicoAppStateCrc(record)) return 0;
  return 1;
}

static inline const PicoAppStateRecord *PicoAppStateRecordAt(uint32_t flash_offset) {
  return (const PicoAppStateRecord *)(PICO_BOOT_XIP_BASE + flash_offset);
}

static inline int PicoAppStateActiveApp(uint8_t *active_app, uint32_t *app_id) {
  PicoBootConfig cfg;
  if (!PicoBootLoadConfig(&cfg)) return 0;
  if (cfg.active_app >= cfg.app_count) return 0;
  if (cfg.active_app >= PICO_BOOT_CONFIG_MAX_APPS) return 0;
  *active_app = cfg.active_app;
  *app_id = cfg.apps[cfg.active_app].app_id;
  if (*app_id == 0) {
    *app_id = (uint32_t)cfg.active_app + 1u;
  }
  return 1;
}

static inline uint32_t PicoAppStateSlotOffset(uint8_t active_app) {
  if (active_app < PICOPRO_APP_STATE_LEGACY_SLOTS) {
    return PICOPRO_APP_STATE_FLASH_OFFSET +
           ((uint32_t)active_app * PICOPRO_APP_STATE_SLOT_BYTES);
  }
  return PICOPRO_APP_STATE_FLASH_OFFSET -
         (((uint32_t)active_app - PICOPRO_APP_STATE_LEGACY_SLOTS + 1u) *
          PICOPRO_APP_STATE_SLOT_BYTES);
}

static inline int PicoAppStateFind(uint32_t base_offset,
                                   uint32_t app_id,
                                   uint32_t schema_tag,
                                   uint16_t payload_size,
                                   const PicoAppStateRecord **latest,
                                   uint32_t *next_offset) {
  const PicoAppStateRecord *best = NULL;
  uint32_t first_empty = 0;

  for (uint32_t offset = base_offset;
       offset < base_offset + PICOPRO_APP_STATE_SLOT_BYTES;
       offset += FLASH_PAGE_SIZE) {
    const PicoAppStateRecord *record = PicoAppStateRecordAt(offset);
    if (record->magic == 0xffffffffu) {
      if (first_empty == 0) first_empty = offset;
      continue;
    }
    if (PicoAppStateRecordValid(record, app_id, schema_tag, payload_size) &&
        (best == NULL || PicoBootSequenceNewerOrEqual(record->sequence, best->sequence))) {
      best = record;
    }
  }

  *latest = best;
  *next_offset = first_empty;
  return best != NULL;
}

static inline int PicoAppStateLoad(uint32_t schema_tag, void *payload, uint16_t payload_size) {
  if (payload_size > PICOPRO_APP_STATE_PAYLOAD_BYTES) return 0;

  uint8_t active_app;
  uint32_t app_id;
  if (!PicoAppStateActiveApp(&active_app, &app_id)) return 0;

  const PicoAppStateRecord *latest;
  uint32_t next_offset;
  if (!PicoAppStateFind(PicoAppStateSlotOffset(active_app), app_id, schema_tag,
                        payload_size, &latest, &next_offset)) {
    return 0;
  }

  memcpy(payload, latest->payload, payload_size);
  return 1;
}

static inline void PicoAppStateProgram(uint32_t flash_offset,
                                       const PicoAppStateRecord *record,
                                       int erase_first) {
  uint8_t page[FLASH_PAGE_SIZE];
  memcpy(page, record, sizeof(page));

  noInterrupts();
  rp2040.idleOtherCore();
  if (erase_first) {
    flash_range_erase(flash_offset, PICOPRO_APP_STATE_SLOT_BYTES);
  }
  flash_range_program(flash_offset, page, sizeof(page));
  rp2040.resumeOtherCore();
  interrupts();
}

static inline int PicoAppStateSave(uint32_t schema_tag,
                                   const void *payload,
                                   uint16_t payload_size) {
  if (payload_size > PICOPRO_APP_STATE_PAYLOAD_BYTES) return 0;

  uint8_t active_app;
  uint32_t app_id;
  if (!PicoAppStateActiveApp(&active_app, &app_id)) return 0;

  const uint32_t base_offset = PicoAppStateSlotOffset(active_app);
  const PicoAppStateRecord *latest;
  uint32_t next_offset;
  PicoAppStateFind(base_offset, app_id, schema_tag, payload_size, &latest, &next_offset);

  if (latest != NULL && memcmp(latest->payload, payload, payload_size) == 0) {
    return 1;
  }

  PicoAppStateRecord record;
  memset(&record, 0xff, sizeof(record));
  record.magic = PICOPRO_APP_STATE_MAGIC;
  record.version = PICOPRO_APP_STATE_VERSION;
  record.payload_size = payload_size;
  record.app_id = app_id;
  record.schema_tag = schema_tag;
  record.sequence = latest != NULL ? latest->sequence + 1u : 1u;
  memcpy(record.payload, payload, payload_size);
  record.crc32 = 0;
  record.crc32 = PicoAppStateCrc(&record);

  if (next_offset == 0) {
    PicoAppStateProgram(base_offset, &record, 1);
  } else {
    PicoAppStateProgram(next_offset, &record, 0);
  }
  return 1;
}

#endif // PICOPRO_APP_STATE_H_
