#!/usr/bin/env python3
"""PicoPro 64x32 OLED UI simulator.

Controls:
  Left / Right  - encoder rotate
  Down press    - encoder switch press
  Down hold     - long press, shown as selector-exit event
  1 / 2         - switch bundled demo menu
  R             - reset current menu state
"""

from __future__ import annotations

import argparse
import math
import time
from dataclasses import dataclass
from typing import Callable


OLED_WIDTH = 64
OLED_HEIGHT = 32
SCREEN_BUFFER_SIZE = OLED_WIDTH * OLED_HEIGHT // 8

DISPLAY_CHAR_WIDTH = 6
DISPLAY_CHAR_HEIGHT = 8
CHARS_X = 10
NAME_X = 0
NAME_Y = DISPLAY_CHAR_HEIGHT
PARAMETER_X = 0
PARAMETER_Y = 24

DEBOUNCE_CYCLES = 100
LOOP_HZ = 1000
LONG_PRESS_MS = 1800

WHITE = 1
BLACK = 0

PICOPIXEL_DIGITS = {
    "0": [0b111, 0b101, 0b101, 0b101, 0b111],
    "1": [0b010, 0b110, 0b010, 0b010, 0b111],
    "2": [0b111, 0b001, 0b111, 0b100, 0b111],
    "3": [0b111, 0b001, 0b111, 0b001, 0b111],
    "4": [0b101, 0b101, 0b111, 0b001, 0b001],
    "5": [0b111, 0b100, 0b111, 0b001, 0b111],
    "6": [0b111, 0b100, 0b111, 0b101, 0b111],
    "7": [0b111, 0b001, 0b010, 0b010, 0b010],
    "8": [0b111, 0b101, 0b111, 0b101, 0b111],
    "9": [0b111, 0b101, 0b111, 0b001, 0b111],
    "/": [0b001, 0b001, 0b010, 0b100, 0b100],
}

GFX_BUILTIN_FONT_HEX = (
    "000000000000005f00000007000700147f147f14242a7f2a12231308646236495620500008070300001c2241000041221c002a1c7f1c2a08083e0808"
    "00807030000808080808000060600020100804023e5149453e00427f400072494949462141494d331814127f1027454545393c4a4949314121110907"
    "3649494936464949291e0000140000004034000000081422411414141414004122140802015909063e415d594e7c1211127c7f494949363e41414122"
    "7f4141413e7f494949417f090909013e414151737f0808087f00417f41002040413f017f081422417f404040407f021c027f7f0408107f3e4141413e"
    "7f090909063e4151215e7f09192946264949493203017f01033f4040403f1f2040201f3f4038403f631408146303047804036159494d43007f414141"
    "0204081020004141417f04020102044040404040000307080020545478407f284444383844444428384444287f385454541800087e090218a4a49c78"
    "7f0804047800447d40002040403d007f1028440000417f40007c047804787c080404783844444438fc1824241818242418fc7c080404084854545424"
    "04043f44243c4040207c1c2040201c3c4030403c44281028444c9090907c4464544c440008364100000077000000413608000201020402"
)
GFX_BUILTIN_FONT = bytes.fromhex(GFX_BUILTIN_FONT_HEX)


@dataclass
class MenuItem:
    name: str
    min_value: int
    max_value: int
    step: int
    ptype: str
    value: int
    text: list[str] | None = None
    handler: Callable[[int], None] | None = None


class Oled64x32:
    def __init__(self) -> None:
        self.buffer = bytearray(SCREEN_BUFFER_SIZE)
        self.cursor_x = 0
        self.cursor_y = 0
        self.text_color = WHITE
        self.bg_color = BLACK

    def clear_display(self) -> None:
        self.buffer[:] = b"\x00" * len(self.buffer)

    def get_pixel(self, x: int, y: int) -> int:
        if not (0 <= x < OLED_WIDTH and 0 <= y < OLED_HEIGHT):
            return 0
        return 1 if self.buffer[(y // 8) * OLED_WIDTH + x] & (1 << (y & 7)) else 0

    def draw_pixel(self, x: int, y: int, color: int = WHITE) -> None:
        if not (0 <= x < OLED_WIDTH and 0 <= y < OLED_HEIGHT):
            return
        index = (y // 8) * OLED_WIDTH + x
        mask = 1 << (y & 7)
        if color:
            self.buffer[index] |= mask
        else:
            self.buffer[index] &= ~mask

    def fill_rect(self, x: int, y: int, w: int, h: int, color: int) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.draw_pixel(xx, yy, color)

    def draw_fast_hline(self, x: int, y: int, w: int, color: int = WHITE) -> None:
        for xx in range(x, x + w):
            self.draw_pixel(xx, y, color)

    def draw_line(self, x0: int, y0: int, x1: int, y1: int, color: int = WHITE) -> None:
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        error = dx + dy
        while True:
            self.draw_pixel(x0, y0, color)
            if x0 == x1 and y0 == y1:
                break
            twice = 2 * error
            if twice >= dy:
                error += dy
                x0 += sx
            if twice <= dx:
                error += dx
                y0 += sy

    def set_cursor(self, x: int, y: int) -> None:
        self.cursor_x = x
        self.cursor_y = y

    def set_text_color(self, fg: int, bg: int = BLACK) -> None:
        self.text_color = fg
        self.bg_color = bg

    def print_text(self, text: str) -> None:
        for ch in text:
            if ch == "\n":
                self.cursor_x = 0
                self.cursor_y += DISPLAY_CHAR_HEIGHT
                continue
            self.draw_char(self.cursor_x, self.cursor_y, ch)
            self.cursor_x += DISPLAY_CHAR_WIDTH

    def draw_char(self, cursor_x: int, cursor_y: int, ch: str) -> None:
        code = ord(ch)
        if code < 0x20 or code > 0x7E:
            code = ord("?")
        offset = (code - 0x20) * 5
        for xx in range(DISPLAY_CHAR_WIDTH):
            line = GFX_BUILTIN_FONT[offset + xx] if xx < 5 else 0
            for yy in range(DISPLAY_CHAR_HEIGHT):
                bit = bool(line & (1 << yy))
                if bit:
                    self.draw_pixel(cursor_x + xx, cursor_y + yy, self.text_color)
                elif self.bg_color != self.text_color:
                    self.draw_pixel(cursor_x + xx, cursor_y + yy, self.bg_color)


class PicoMenu:
    PARAM_SELECT = 0
    PARAM_INPUT = 1
    WAITBUTTONRELEASE1 = 2
    WAITBUTTONRELEASE2 = 3

    def __init__(self, oled: Oled64x32, menus: list[MenuItem]) -> None:
        self.oled = oled
        self.menus = menus
        self.state = self.PARAM_SELECT
        self.index = 0
        self.debounce_counter = 0
        self.encoder_delta = 0
        self.button_pressed = False
        self.display_timer_ms = 0
        self.draw_menu(0)

    def reset(self) -> None:
        self.state = self.PARAM_SELECT
        self.index = 0
        self.debounce_counter = 0
        self.encoder_delta = 0
        self.button_pressed = False
        self.draw_menu(0)

    def rotate(self, delta: int) -> None:
        self.encoder_delta += delta

    def set_button(self, pressed: bool) -> None:
        self.button_pressed = pressed

    def digital_read_button(self) -> bool:
        return not self.button_pressed

    def get_encoder_value(self) -> int:
        value = self.encoder_delta
        self.encoder_delta = 0
        return value

    def updatedisplay(self) -> None:
        self.display_timer_ms = int(time.monotonic() * 1000)

    def draw_text_centered(self, text: str, x: int, y: int) -> None:
        text = text.lower()[:CHARS_X]
        space = DISPLAY_CHAR_WIDTH * (CHARS_X - len(text)) // 2
        self.oled.set_cursor(x + space, y)
        self.oled.print_text(text)

    def draw_picopixel_text(self, text: str, x: int, y: int) -> None:
        cursor_x = x
        for ch in text:
            rows = PICOPIXEL_DIGITS.get(ch)
            if rows is None:
                cursor_x += 2
                continue
            for yy, row in enumerate(rows):
                for xx in range(3):
                    if row & (1 << (2 - xx)):
                        self.oled.draw_pixel(cursor_x + xx, y + yy, WHITE)
            cursor_x += 4

    def draw_edit_selector(self, index: int) -> None:
        item = self.menus[index]
        length = min(len(item.name), CHARS_X)
        space = DISPLAY_CHAR_WIDTH * (CHARS_X - length) // 2
        self.oled.draw_fast_hline(space, DISPLAY_CHAR_HEIGHT + 2, length * DISPLAY_CHAR_WIDTH, WHITE)
        self.updatedisplay()

    def draw_parameter_value(self, index: int) -> None:
        item = self.menus[index]
        self.oled.fill_rect(PARAMETER_X, OLED_HEIGHT - (DISPLAY_CHAR_HEIGHT + 3), DISPLAY_CHAR_WIDTH * CHARS_X, DISPLAY_CHAR_HEIGHT + 3, BLACK)
        if item.step == 0:
            return
        value = item.value
        if item.ptype == "integer":
            text = f"{value:d}"
        elif item.ptype == "float":
            text = f"{value / 1000:1.2f}"
        elif item.ptype == "text":
            if item.text:
                safe = max(0, min(value, len(item.text) - 1))
                text = item.text[safe]
            else:
                text = ""
        else:
            text = "        "
        self.draw_text_centered(text, PARAMETER_X, PARAMETER_Y)

    def draw_menu(self, index: int) -> None:
        self.oled.clear_display()
        self.draw_picopixel_text(f"{index + 1}/{len(self.menus)}", 0, 1)
        self.draw_text_centered(self.menus[index].name, NAME_X, NAME_Y)
        self.draw_parameter_value(index)
        self.updatedisplay()

    def tick(self) -> None:
        enc = self.get_encoder_value()
        if self.state == self.PARAM_SELECT:
            if enc:
                self.index += enc
                if self.index < 0:
                    self.index = len(self.menus) - 1
                if self.index > len(self.menus) - 1:
                    self.index = 0
                self.draw_menu(self.index)
            if not self.digital_read_button():
                self.draw_edit_selector(self.index)
                self.state = self.WAITBUTTONRELEASE1
                self.debounce_counter = DEBOUNCE_CYCLES
        elif self.state == self.WAITBUTTONRELEASE1:
            if self.digital_read_button():
                self.debounce_counter -= 1
                if self.debounce_counter <= 0:
                    self.state = self.PARAM_INPUT
        elif self.state == self.PARAM_INPUT:
            if enc:
                item = self.menus[self.index]
                item.value += enc * item.step
                item.value = max(item.min_value, min(item.max_value, item.value))
                if item.handler:
                    item.handler(item.value)
                self.draw_parameter_value(self.index)
                self.updatedisplay()
            if not self.digital_read_button():
                self.draw_menu(self.index)
                self.debounce_counter = DEBOUNCE_CYCLES
                self.state = self.WAITBUTTONRELEASE2
        elif self.state == self.WAITBUTTONRELEASE2:
            if self.digital_read_button():
                self.debounce_counter -= 1
                if self.debounce_counter <= 0:
                    self.state = self.PARAM_SELECT
        else:
            self.state = self.PARAM_SELECT


class WavetableMenu(PicoMenu):
    def draw_parameter_value(self, index: int) -> None:
        if index == 1:
            self.draw_menu(index)
        else:
            super().draw_parameter_value(index)

    def draw_menu(self, index: int) -> None:
        if index != 1:
            super().draw_menu(index)
            return
        self.oled.clear_display()
        center_wave = self.menus[1].value
        first: list[tuple[int, int]] = []
        last: list[tuple[int, int]] = []
        for layer in range(5):
            rel = layer - 2
            wave = max(0, min(self.menus[1].max_value, center_wave + rel))
            base_y = 4 + layer * 3
            x_offset = (4 - layer) * 2
            previous: tuple[int, int] | None = None
            for point in range(24):
                phase = point / 24
                harmonic = 1 + wave % 5
                sample = math.sin(phase * math.tau) * 2.5 + math.sin(phase * math.tau * harmonic) * 0.8
                current = (5 + x_offset + point * 2, max(0, min(19, round(base_y - sample))))
                if point == 0:
                    first.append(current)
                if previous is not None:
                    if rel == 0:
                        self.oled.draw_line(*previous, *current, WHITE)
                        self.oled.draw_line(previous[0], previous[1] + 1, current[0], current[1] + 1, WHITE)
                    elif point & 1:
                        self.oled.draw_line(*previous, *current, WHITE)
                if point == 23:
                    last.append(current)
                previous = current
        for layer in range(1, 5):
            self.oled.draw_line(*first[layer - 1], *first[layer], WHITE)
            self.oled.draw_line(*last[layer - 1], *last[layer], WHITE)
        self.draw_text_centered(f"wav {center_wave}", 0, 24)
        self.updatedisplay()


def delay_menu() -> list[MenuItem]:
    return [
        MenuItem("delay ms", 0, 1000, 10, "integer", 500),
        MenuItem("fdback", 0, 1000, 10, "float", 100),
        MenuItem("x fdback", 0, 1000, 10, "float", 100),
        MenuItem("mix", 0, 1000, 10, "float", 100),
        MenuItem("level", 0, 1000, 10, "float", 800),
    ]


def reverb_menu() -> list[MenuItem]:
    return [
        MenuItem("feedback", 0, 100, 1, "integer", 50),
        MenuItem("cutoff", 550, 44100 // 2, 500, "integer", 5050),
        MenuItem("mix", 0, 100, 1, "integer", 50),
        MenuItem("level", 0, 100, 1, "integer", 75),
    ]


def wavetable_menu() -> list[MenuItem]:
    return [
        MenuItem("bank", 0, 1, 1, "text", 0, ["waves", "basic"]),
        MenuItem("wave", 0, 165, 1, "integer", 0),
        MenuItem("freq", 20, 2000, 1, "integer", 90),
        MenuItem("level", 0, 100, 1, "integer", 75),
        MenuItem("gate", 0, 2, 1, "text", 1, ["off", "cv1", "cv2"]),
        MenuItem("atk", 0, 100, 1, "integer", 1),
        MenuItem("dec", 0, 100, 1, "integer", 15),
        MenuItem("sus", 0, 100, 1, "integer", 80),
        MenuItem("rel", 0, 100, 1, "integer", 10),
    ]


class SimulatorApp:
    def __init__(self, scale: int, demo: str) -> None:
        try:
            import tkinter as tk
        except Exception as exc:
            raise SystemExit(
                "Tkinter is not available in this Python. Use the dependency-free "
                "browser simulator instead:\n  python3 sims/serve.py\n"
            ) from exc
        self.tk = tk
        self.oled = Oled64x32()
        self.scale = scale
        self.root = self.tk.Tk()
        self.root.title("PicoPro OLED UI Simulator")
        self.canvas = self.tk.Canvas(
            self.root,
            width=OLED_WIDTH * scale,
            height=OLED_HEIGHT * scale,
            bg="#071007",
            highlightthickness=0,
        )
        self.canvas.grid(row=0, column=0, padx=12, pady=(12, 4))
        self.status = self.tk.StringVar()
        self.tk.Label(self.root, textvariable=self.status, anchor="w", font=("Menlo", 11)).grid(row=1, column=0, sticky="ew", padx=12, pady=(0, 12))
        self.root.bind("<KeyPress>", self.on_key_press)
        self.root.bind("<KeyRelease>", self.on_key_release)
        self.button_down_at: float | None = None
        self.long_press_fired = False
        self.demo_name = ""
        self.menu: PicoMenu
        self.load_demo(demo)
        self.render()
        self.root.after(1, self.loop)

    def load_demo(self, name: str) -> None:
        if name == "wavetable":
            menus = wavetable_menu()
        elif name == "reverb":
            menus = reverb_menu()
        else:
            name = "delay"
            menus = delay_menu()
        self.demo_name = name
        self.menu = WavetableMenu(self.oled, menus) if name == "wavetable" else PicoMenu(self.oled, menus)
        self.set_status("ready")

    def set_status(self, event: str) -> None:
        state_names = {
            PicoMenu.PARAM_SELECT: "PARAM_SELECT",
            PicoMenu.PARAM_INPUT: "PARAM_INPUT",
            PicoMenu.WAITBUTTONRELEASE1: "WAIT_RELEASE1",
            PicoMenu.WAITBUTTONRELEASE2: "WAIT_RELEASE2",
        }
        self.status.set(
            f"{self.demo_name.upper()} | {state_names.get(self.menu.state, '?')} | "
            f"item={self.menu.index + 1}/{len(self.menu.menus)} | event={event} | "
            "←/→ rotate, ↓ press/hold, 1 Delay, 2 Reverb, 3 Wavetable, R reset"
        )

    def on_key_press(self, event: tk.Event) -> None:
        key = event.keysym
        if key == "Left":
            self.menu.rotate(-1)
            self.set_status("encoder left")
        elif key == "Right":
            self.menu.rotate(1)
            self.set_status("encoder right")
        elif key == "Down":
            if self.button_down_at is None:
                self.button_down_at = time.monotonic()
                self.long_press_fired = False
                self.menu.set_button(True)
                self.set_status("button down")
        elif key == "1":
            self.load_demo("delay")
        elif key == "2":
            self.load_demo("reverb")
        elif key == "3":
            self.load_demo("wavetable")
        elif key.lower() == "r":
            self.menu.reset()
            self.set_status("reset")

    def on_key_release(self, event: tk.Event) -> None:
        if event.keysym == "Down":
            self.menu.set_button(False)
            self.button_down_at = None
            self.long_press_fired = False
            self.set_status("button up")

    def loop(self) -> None:
        if self.button_down_at is not None and not self.long_press_fired:
            held_ms = int((time.monotonic() - self.button_down_at) * 1000)
            if held_ms >= LONG_PRESS_MS:
                self.long_press_fired = True
                self.set_status("LONG PRESS -> selector exit")
        self.menu.tick()
        self.render()
        self.root.after(max(1, 1000 // LOOP_HZ), self.loop)

    def render(self) -> None:
        self.canvas.delete("all")
        s = self.scale
        for y in range(OLED_HEIGHT):
            for x in range(OLED_WIDTH):
                if self.oled.get_pixel(x, y):
                    self.canvas.create_rectangle(x * s, y * s, (x + 1) * s - 1, (y + 1) * s - 1, fill="#d7ffd7", outline="")
                else:
                    self.canvas.create_rectangle(x * s, y * s, (x + 1) * s - 1, (y + 1) * s - 1, fill="#001000", outline="")

    def run(self) -> None:
        self.root.mainloop()


def main() -> None:
    parser = argparse.ArgumentParser(description="Simulate PicoPro 64x32 OLED UI")
    parser.add_argument("--scale", type=int, default=10, help="pixel scale factor")
    parser.add_argument("--demo", choices=["delay", "reverb", "wavetable"], default="delay")
    args = parser.parse_args()
    SimulatorApp(scale=max(2, args.scale), demo=args.demo).run()


if __name__ == "__main__":
    main()
