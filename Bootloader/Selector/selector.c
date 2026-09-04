// PicoPro OLED app selector bootloader.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/structs/watchdog.h"
#include "pico/stdlib.h"

#if defined(__arm__)
#include "hardware/structs/scb.h"
#endif

#include "boot_config.h"
#include "selector_app_icons.h"
#include "fm_icon_animation.h"

#define PIN_WIRE_SDA 4u
#define PIN_WIRE_SCL 5u
#define ENCA_IN 7u
#define ENCB_IN 6u
#define ENCSW_IN 8u

#define OLED_ADDR 0x3cu
#define OLED_WIDTH 64u
#define OLED_HEIGHT 32u
#define OLED_PAGES (OLED_HEIGHT / 8u)
#define XIP_BASE_ADDR 0x10000000u
#define SRAM_BASE_ADDR 0x20000000u
#define SRAM_END_ADDR 0x20082000u
#define SELECT_DEBOUNCE_MS 30u
#define SELECT_ENCODER_STEPS 2
#define BOOT_ANIMATION_FPS 15u
#define SELECTOR_ANIMATION_FPS 15u
#define BOOT_ANIMATION_CYCLES 3u
#define PICOPRO_SELECTOR_RETURN_MAGIC 0x50435254u
#define OLED_MAX_CMD_BYTES 32u
#define OLED_I2C_TIMEOUT_US 20000u

typedef void (*AppEntry)(void);

static uint8_t oled_buffer[OLED_WIDTH * OLED_PAGES];
static uint8_t oled_packet[1u + sizeof(oled_buffer)] = {0x40u};
static uint8_t encoder_last;
static int8_t encoder_delta;

static void oled_clear(void);
static void oled_flush(void);

static void i2c_bus_recover(void) {
  gpio_init(PIN_WIRE_SDA);
  gpio_init(PIN_WIRE_SCL);
  gpio_pull_up(PIN_WIRE_SDA);
  gpio_pull_up(PIN_WIRE_SCL);
  gpio_set_dir(PIN_WIRE_SDA, GPIO_IN);
  gpio_set_dir(PIN_WIRE_SCL, GPIO_OUT);
  gpio_put(PIN_WIRE_SCL, 1);
  sleep_us(5);

  for (uint8_t i = 0; i < 9u && !gpio_get(PIN_WIRE_SDA); ++i) {
    gpio_put(PIN_WIRE_SCL, 0);
    sleep_us(5);
    gpio_put(PIN_WIRE_SCL, 1);
    sleep_us(5);
  }

  gpio_set_dir(PIN_WIRE_SDA, GPIO_OUT);
  gpio_put(PIN_WIRE_SDA, 0);
  sleep_us(5);
  gpio_put(PIN_WIRE_SCL, 1);
  sleep_us(5);
  gpio_put(PIN_WIRE_SDA, 1);
  sleep_us(5);
}

static void oled_write(const uint8_t *data, size_t len) {
  i2c_write_timeout_us(i2c0, OLED_ADDR, data, len, false, OLED_I2C_TIMEOUT_US);
}

static void oled_cmds(const uint8_t *cmds, size_t len) {
  uint8_t packet[1u + OLED_MAX_CMD_BYTES];
  if (len > OLED_MAX_CMD_BYTES) return;
  packet[0] = 0x00;
  memcpy(&packet[1], cmds, len);
  oled_write(packet, len + 1u);
}

static void oled_init(void) {
  i2c_bus_recover();
  i2c_init(i2c0, 400000);
  gpio_set_function(PIN_WIRE_SDA, GPIO_FUNC_I2C);
  gpio_set_function(PIN_WIRE_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(PIN_WIRE_SDA);
  gpio_pull_up(PIN_WIRE_SCL);
  sleep_ms(50);

  const uint8_t init[] = {
      0xae, 0xd5, 0x80, 0xa8, OLED_HEIGHT - 1u, 0xd3, 0x00, 0x40,
      0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x12, 0x81, 0xcf,
      0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0x2e, 0xaf,
  };
  oled_cmds(init, sizeof(init));
  oled_clear();
  oled_flush();
}

static void oled_clear(void) {
  memset(oled_buffer, 0, sizeof(oled_buffer));
}

static void oled_flush(void) {
  const uint8_t window[] = {0x22, 0x00, 0xff, 0x21, 0x20, 0x20 + OLED_WIDTH - 1u};
  oled_cmds(window, sizeof(window));
  memcpy(&oled_packet[1], oled_buffer, sizeof(oled_buffer));
  oled_write(oled_packet, sizeof(oled_packet));
}

static uint8_t valid_app_count(const PicoBootConfig *cfg) {
  uint8_t count = cfg->app_count;
  if (count > PICOPRO_SELECTOR_APP_ICON_COUNT) count = PICOPRO_SELECTOR_APP_ICON_COUNT;
  return count;
}

static const PicoProSelectorIcon *app_icon(uint8_t app) {
  if (app >= PICOPRO_SELECTOR_APP_ICON_COUNT) return NULL;
  const PicoProSelectorIcon *icon = &PICOPRO_SELECTOR_APP_ICONS[app];
  if (icon->frames == NULL || icon->frame_count == 0) return NULL;
  return icon;
}

static void draw_raw_frame(const uint8_t *frame) {
  memcpy(oled_buffer, frame, sizeof(oled_buffer));
  oled_flush();
}

static void draw_icon_frame(const PicoProSelectorIcon *icon, uint8_t frame) {
  draw_raw_frame(icon->frames[frame]);
}

static void draw_selected_app(const PicoProSelectorIcon *icon) {
  if (icon != NULL) {
    draw_icon_frame(icon, 0);
  } else {
    oled_clear();
    oled_flush();
  }
}

static void play_boot_animation(void) {
  for (uint8_t cycle = 0; cycle < BOOT_ANIMATION_CYCLES; ++cycle) {
    for (uint8_t frame = 0; frame < PICOPRO_FM_ICON_FRAME_COUNT; ++frame) {
      draw_raw_frame(PICOPRO_FM_ICON_FRAMES[frame]);
      sleep_ms(1000u / BOOT_ANIMATION_FPS);
    }
  }
}

static bool take_return_from_app_flag(void) {
  if (watchdog_hw->scratch[0] != PICOPRO_SELECTOR_RETURN_MAGIC) {
    return false;
  }
  watchdog_hw->scratch[0] = 0;
  return true;
}

static void encoder_init(void) {
  gpio_init(ENCA_IN);
  gpio_init(ENCB_IN);
  gpio_init(ENCSW_IN);
  gpio_set_dir(ENCA_IN, GPIO_IN);
  gpio_set_dir(ENCB_IN, GPIO_IN);
  gpio_set_dir(ENCSW_IN, GPIO_IN);
  gpio_pull_up(ENCA_IN);
  gpio_pull_up(ENCB_IN);
  gpio_pull_up(ENCSW_IN);
  encoder_last = (gpio_get(ENCA_IN) ? 2u : 0u) | (gpio_get(ENCB_IN) ? 1u : 0u);
}

static int8_t encoder_read(void) {
  static const int8_t table[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0,
  };
  const uint8_t current = (gpio_get(ENCA_IN) ? 2u : 0u) | (gpio_get(ENCB_IN) ? 1u : 0u);
  const uint8_t index = (encoder_last << 2u) | current;
  encoder_last = current;
  encoder_delta += table[index & 0x0fu];
  if (encoder_delta >= SELECT_ENCODER_STEPS) {
    encoder_delta = 0;
    return 1;
  }
  if (encoder_delta <= -SELECT_ENCODER_STEPS) {
    encoder_delta = 0;
    return -1;
  }
  return 0;
}

static bool button_pressed(void) {
  return !gpio_get(ENCSW_IN);
}

static void wait_button_release(void) {
  while (button_pressed()) {
    sleep_ms(1);
  }
  sleep_ms(SELECT_DEBOUNCE_MS);
}

static bool app_vector_valid(const PicoBootAppSlot *slot) {
  if ((slot->flash_offset & 3u) != 0u) return false;
  if ((slot->vector_offset & 3u) != 0u) return false;
  if (slot->flash_offset < PICO_BOOT_FIRST_APP_OFFSET) return false;
  if (slot->flash_offset >= PICO_FLASH_SIZE_BYTES) return false;
  if (slot->max_size > PICO_FLASH_SIZE_BYTES - slot->flash_offset) return false;
  if (slot->vector_offset > slot->max_size) return false;
  if (slot->max_size - slot->vector_offset < 2u * sizeof(uint32_t)) return false;

  const uint32_t vector_addr = XIP_BASE_ADDR + slot->flash_offset + slot->vector_offset;
  const uint32_t *vector = (const uint32_t *)vector_addr;
  const uint32_t sp = vector[0];
  const uint32_t reset = vector[1];
  const uint32_t app_start = XIP_BASE_ADDR + slot->flash_offset;
  const uint32_t app_end = app_start + slot->max_size;

  if ((sp & 7u) != 0u) return false;
  if (sp < SRAM_BASE_ADDR || sp > SRAM_END_ADDR) return false;
  if ((reset & 1u) == 0) return false;
  if ((reset & ~1u) < app_start || (reset & ~1u) >= app_end) return false;
  return true;
}

static bool slot_has_app(const PicoBootConfig *cfg, uint8_t app) {
  if (app >= cfg->app_count) return false;
  const PicoBootAppSlot *slot = &cfg->apps[app];
  return (slot->flags & PICO_BOOT_FLAG_VALID) && app_vector_valid(slot);
}

static void jump_to_app(const PicoBootAppSlot *slot) {
  const uint32_t vector_addr = XIP_BASE_ADDR + slot->flash_offset + slot->vector_offset;
  const uint32_t *vector = (const uint32_t *)vector_addr;
  const uint32_t app_sp = vector[0];
  const uint32_t app_reset = vector[1];

  oled_clear();
  oled_flush();
  sleep_ms(5);
  __asm volatile("cpsid i");

#if defined(__arm__)
  scb_hw->vtor = vector_addr;
  __asm volatile("msr msp, %0\n"
                 "bx %1\n"
                 :
                 : "r"(app_sp), "r"(app_reset)
                 :);
#else
  (void)app_sp;
  ((AppEntry)app_reset)();
#endif

  while (true) tight_loop_contents();
}

static void apply_cpu_frequency(const PicoBootConfig *cfg) {
  if (cfg->cpu_hz >= 48000000u && cfg->cpu_hz <= 300000000u) {
    set_sys_clock_khz(cfg->cpu_hz / 1000u, true);
  }
}

int main(void) {
  encoder_init();

  PicoBootConfig cfg;
  pico_boot_config_load(&cfg);
  apply_cpu_frequency(&cfg);

  const bool returned_from_app = take_return_from_app_flag();
  oled_init();
  if (!returned_from_app) {
    play_boot_animation();
  }

  uint8_t count = valid_app_count(&cfg);
  if (count == 0) count = 1;
  uint8_t selected = cfg.active_app < count ? cfg.active_app : 0;
  if (!returned_from_app && slot_has_app(&cfg, selected)) {
    jump_to_app(&cfg.apps[selected]);
  }

  uint8_t selector_frame = 0;
  const PicoProSelectorIcon *selected_icon = app_icon(selected);
  uint8_t selected_frame_count = selected_icon != NULL ? selected_icon->frame_count : 0;
  absolute_time_t next_selector_frame = make_timeout_time_ms(1000u / SELECTOR_ANIMATION_FPS);
  draw_selected_app(selected_icon);
  wait_button_release();

  while (true) {
    const int8_t step = encoder_read();
    if (step != 0) {
      if (step > 0) {
        selected = (selected + 1u) % count;
      } else {
        selected = selected == 0 ? count - 1u : selected - 1u;
      }
      selector_frame = 0;
      selected_icon = app_icon(selected);
      selected_frame_count = selected_icon != NULL ? selected_icon->frame_count : 0;
      next_selector_frame = make_timeout_time_ms(1000u / SELECTOR_ANIMATION_FPS);
      draw_selected_app(selected_icon);
    } else if (absolute_time_diff_us(get_absolute_time(), next_selector_frame) <= 0) {
      if (selected_frame_count != 0) {
        if (++selector_frame >= selected_frame_count) selector_frame = 0;
        draw_icon_frame(selected_icon, selector_frame);
      }
      next_selector_frame = make_timeout_time_ms(1000u / SELECTOR_ANIMATION_FPS);
    }

    if (button_pressed()) {
      sleep_ms(SELECT_DEBOUNCE_MS);
      if (button_pressed() && slot_has_app(&cfg, selected)) {
        if (cfg.active_app != selected) {
          cfg.active_app = selected;
          pico_boot_config_save(&cfg);
        }
        wait_button_release();
        jump_to_app(&cfg.apps[selected]);
      }
      wait_button_release();
    }

    sleep_ms(1);
  }
}
