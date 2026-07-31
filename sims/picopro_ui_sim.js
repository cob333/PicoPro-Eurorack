"use strict";

const OLED_WIDTH = 64;
const OLED_HEIGHT = 32;
const SCREEN_BUFFER_SIZE = OLED_WIDTH * OLED_HEIGHT / 8;
const DISPLAY_CHAR_WIDTH = 6;
const DISPLAY_CHAR_HEIGHT = 8;
const CHARS_X = 10;
const NAME_X = 0;
const NAME_Y = DISPLAY_CHAR_HEIGHT;
const PARAMETER_X = 0;
const PARAMETER_Y = 24;
const MENU_RELEASE_DEBOUNCE_CYCLES = 0;
const LOOP_MS = 1;
const SELECTOR_ANIMATION_MS = 1000 / 15;
const LONG_PRESS_MS = 1800;
const CLICK_MIN_MS = 20;
const CLICK_MAX_MS = 260;
const DOUBLE_CLICK_MS = 250;
const CV_DISPLAY_INTERVAL_MS = 67;
const KNOB_CENTER_X = 32;
const KNOB_CENTER_Y = 10;
const KNOB_RADIUS = 8;
const KNOB_LEFT_ARROW_X = 7;
const KNOB_RIGHT_ARROW_X = 56;
const KNOB_ARROW_Y = 16;
const KNOB_NAV_ANIM_MS = 18;
const CV_UI_OFF = 0;
const CV_UI_SELECT_INPUT = 1;
const CV_UI_AMOUNT = 2;
const CV_UI_WAIT_RELEASE_DONE = 3;
const CV_VOCT_AMOUNT = 11;
const CV_DELTA_DEADBAND_VOLTS = 0.012;
const PICOPIXEL_DIGITS = {
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
};
const KNOB_POINTS = [
  [-3, 6], [-4, 5], [-5, 4], [-6, 3], [-7, 2], [-7, 1],
  [-7, 0], [-7, -1], [-7, -2], [-6, -4], [-5, -4], [-4, -5],
  [-3, -6], [-2, -7], [-1, -7], [0, -7], [1, -7], [2, -7],
  [3, -6], [4, -5], [5, -4], [6, -4], [7, -2], [7, -1],
  [7, 0], [7, 1], [7, 2], [6, 3], [5, 4], [4, 5], [3, 6],
];

const WHITE = 1;
const BLACK = 0;
const GFX_BUILTIN_FONT_HEX =
  "000000000000005f00000007000700147f147f14242a7f2a12231308646236495620500008070300001c2241000041221c002a1c7f1c2a08083e0808" +
  "00807030000808080808000060600020100804023e5149453e00427f400072494949462141494d331814127f1027454545393c4a4949314121110907" +
  "3649494936464949291e0000140000004034000000081422411414141414004122140802015909063e415d594e7c1211127c7f494949363e41414122" +
  "7f4141413e7f494949417f090909013e414151737f0808087f00417f41002040413f017f081422417f404040407f021c027f7f0408107f3e4141413e" +
  "7f090909063e4151215e7f09192946264949493203017f01033f4040403f1f2040201f3f4038403f631408146303047804036159494d43007f414141" +
  "0204081020004141417f04020102044040404040000307080020545478407f284444383844444428384444287f385454541800087e090218a4a49c78" +
  "7f0804047800447d40002040403d007f1028440000417f40007c047804787c080404783844444438fc1824241818242418fc7c080404084854545424" +
  "04043f44243c4040207c1c2040201c3c4030403c44281028444c9090907c4464544c440008364100000077000000413608000201020402";
const GFX_BUILTIN_FONT = {
  name: "Adafruit GFX 5x7",
  kind: "builtin",
  first: 0x20,
  last: 0x7e,
  yAdvance: 8,
  bytes: Array.from({ length: GFX_BUILTIN_FONT_HEX.length / 2 }, (_, i) =>
    Number.parseInt(GFX_BUILTIN_FONT_HEX.slice(i * 2, i * 2 + 2), 16)
  ),
};

function parseFontHeader(text) {
  const bitmapMatches = [...text.matchAll(/const\s+uint8_t\s+(\w+Bitmaps)\[\]\s+PROGMEM\s*=\s*\{([\s\S]*?)\};/g)];
  const glyphMatches = [...text.matchAll(/const\s+GFXglyph\s+(\w+Glyphs)\[\]\s+PROGMEM\s*=\s*\{([\s\S]*?)\};/g)];
  const fontMatch = text.match(
    /const\s+GFXfont\s+(\w+)\s+PROGMEM\s*=\s*\{\s*(?:\(uint8_t\s*\*\)\s*)?(\w+Bitmaps)\s*,\s*(?:\(GFXglyph\s*\*\)\s*)?(\w+Glyphs)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(\d+)\s*\}/
  );
  if (!bitmapMatches.length || !glyphMatches.length || !fontMatch) {
    throw new Error("Cannot parse GFX font header");
  }

  const bitmapName = fontMatch[2];
  const glyphName = fontMatch[3];
  const bitmapMatch = bitmapMatches.find((match) => match[1] === bitmapName) ?? bitmapMatches[0];
  const glyphMatch = glyphMatches.find((match) => match[1] === glyphName) ?? glyphMatches[0];
  const bitmaps = [...bitmapMatch[2].matchAll(/0x[0-9A-Fa-f]+/g)].map((m) => Number.parseInt(m[0], 16));
  const glyphs = [...glyphMatch[2].matchAll(/\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}/g)]
    .map((m) => ({
      bitmapOffset: Number(m[1]),
      width: Number(m[2]),
      height: Number(m[3]),
      xAdvance: Number(m[4]),
      xOffset: Number(m[5]),
      yOffset: Number(m[6]),
    }));
  return {
    name: fontMatch[1],
    bitmaps,
    glyphs,
    first: parseNumberLiteral(fontMatch[4]),
    last: parseNumberLiteral(fontMatch[5]),
    yAdvance: Number(fontMatch[6]),
  };
}

function parseNumberLiteral(value) {
  return value.startsWith("0x") || value.startsWith("0X")
    ? Number.parseInt(value, 16)
    : Number.parseInt(value, 10);
}

function parseSelectorIcons(text) {
  const arrays = new Map();
  const arrayRe = /static const uint8_t\s+(PICOPRO_SELECTOR_[A-Z0-9_]+_ICON_FRAMES)\[\]\s*\[PICOPRO_SELECTOR_ICON_FRAME_BYTES\]\s*=\s*\{([\s\S]*?)\n\};/g;
  for (const match of text.matchAll(arrayRe)) {
    const frames = [];
    const frameRe = /\{\s*\/\/ frame \d+([\s\S]*?)\n\s*\},/g;
    for (const frameMatch of match[2].matchAll(frameRe)) {
      const values = [...frameMatch[1].matchAll(/0x[0-9A-Fa-f]+/g)].map((m) => Number.parseInt(m[0], 16));
      if (values.length === SCREEN_BUFFER_SIZE) frames.push(values);
    }
    arrays.set(match[1], frames);
  }

  const tableMatch = text.match(/static const PicoProSelectorIcon PICOPRO_SELECTOR_APP_ICONS\[\]\s*=\s*\{([\s\S]*?)\n\};/);
  if (!tableMatch) return [];

  const table = [];
  const rowRe = /\{\s*(NULL|PICOPRO_SELECTOR_[A-Z0-9_]+_ICON_FRAMES)\s*,\s*(\d+)u\s*\}/g;
  for (const row of tableMatch[1].matchAll(rowRe)) {
    const symbol = row[1];
    table.push(symbol === "NULL" ? [] : (arrays.get(symbol) ?? []));
  }
  return table;
}

class Oled64x32 {
  constructor(font = GFX_BUILTIN_FONT) {
    this.font = font ?? GFX_BUILTIN_FONT;
    this.buffer = new Uint8Array(SCREEN_BUFFER_SIZE);
    this.cursorX = 0;
    this.cursorY = 0;
    this.textColor = WHITE;
    this.bgColor = BLACK;
  }

  clearDisplay() {
    this.buffer.fill(0);
  }

  setFrame(frame) {
    if (!frame) {
      this.clearDisplay();
      return;
    }
    this.buffer.set(frame.slice(0, SCREEN_BUFFER_SIZE));
  }

  setCursor(x, y) {
    this.cursorX = x;
    this.cursorY = y;
  }

  setTextColor(fg, bg = BLACK) {
    this.textColor = fg;
    this.bgColor = bg;
  }

  drawPixel(x, y, color = WHITE) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    const index = Math.floor(y / 8) * OLED_WIDTH + x;
    const mask = 1 << (y & 7);
    if (color) this.buffer[index] |= mask;
    else this.buffer[index] &= ~mask;
  }

  getPixel(x, y) {
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return 0;
    return (this.buffer[Math.floor(y / 8) * OLED_WIDTH + x] & (1 << (y & 7))) ? 1 : 0;
  }

  fillRect(x, y, w, h, color) {
    for (let yy = y; yy < y + h; yy += 1) {
      for (let xx = x; xx < x + w; xx += 1) this.drawPixel(xx, yy, color);
    }
  }

  drawFastHLine(x, y, w, color = WHITE) {
    for (let xx = x; xx < x + w; xx += 1) this.drawPixel(xx, y, color);
  }

  drawLine(x0, y0, x1, y1, color = WHITE) {
    let dx = Math.abs(x1 - x0);
    let sx = x0 < x1 ? 1 : -1;
    let dy = -Math.abs(y1 - y0);
    let sy = y0 < y1 ? 1 : -1;
    let err = dx + dy;
    while (true) {
      this.drawPixel(x0, y0, color);
      if (x0 === x1 && y0 === y1) break;
      const e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }

  drawCircle(cx, cy, r, color = WHITE) {
    let x = -r;
    let y = 0;
    let err = 2 - 2 * r;
    do {
      this.drawPixel(cx - x, cy + y, color);
      this.drawPixel(cx - y, cy - x, color);
      this.drawPixel(cx + x, cy - y, color);
      this.drawPixel(cx + y, cy + x, color);
      const current = err;
      if (current <= y) err += ++y * 2 + 1;
      if (current > x || err > y) err += ++x * 2 + 1;
    } while (x < 0);
  }

  glyphFor(ch) {
    if (this.font.kind === "builtin") return null;
    let code = ch.codePointAt(0);
    if (code < this.font.first || code > this.font.last) code = "?".codePointAt(0);
    return this.font.glyphs[code - this.font.first] ?? null;
  }

  glyphBit(glyph, x, y) {
    const bitIndex = y * glyph.width + x;
    const byte = this.font.bitmaps[glyph.bitmapOffset + Math.floor(bitIndex / 8)];
    return Boolean(byte & (0x80 >> (bitIndex & 7)));
  }

  printText(text) {
    for (const ch of text) {
      if (ch === "\n") {
        this.cursorX = 0;
        this.cursorY += this.font.yAdvance ?? DISPLAY_CHAR_HEIGHT;
        continue;
      }
      this.drawChar(this.cursorX, this.cursorY, ch);
      if (this.font.kind === "builtin") {
        this.cursorX += DISPLAY_CHAR_WIDTH;
      } else {
        const glyph = this.glyphFor(ch);
        this.cursorX += glyph ? glyph.xAdvance : DISPLAY_CHAR_WIDTH;
      }
    }
  }

  drawChar(cursorX, cursorY, ch) {
    if (this.font.kind === "builtin") {
      this.drawBuiltinChar(cursorX, cursorY, ch);
      return;
    }
    const glyph = this.glyphFor(ch);
    if (!glyph) return;
    const x0 = cursorX + glyph.xOffset;
    const y0 = cursorY + glyph.yOffset;
    if (this.bgColor === BLACK) {
      this.fillRect(cursorX, cursorY - DISPLAY_CHAR_HEIGHT, glyph.xAdvance, this.font.yAdvance, BLACK);
    }
    for (let y = 0; y < glyph.height; y += 1) {
      for (let x = 0; x < glyph.width; x += 1) {
        if (this.glyphBit(glyph, x, y)) this.drawPixel(x0 + x, y0 + y, this.textColor);
      }
    }
  }

  drawBuiltinChar(cursorX, cursorY, ch) {
    let code = ch.codePointAt(0);
    if (code < GFX_BUILTIN_FONT.first || code > GFX_BUILTIN_FONT.last) {
      code = "?".codePointAt(0);
    }
    const offset = (code - GFX_BUILTIN_FONT.first) * 5;
    for (let x = 0; x < DISPLAY_CHAR_WIDTH; x += 1) {
      const line = x < 5 ? GFX_BUILTIN_FONT.bytes[offset + x] : 0;
      for (let y = 0; y < DISPLAY_CHAR_HEIGHT; y += 1) {
        const bit = (line & (1 << y)) !== 0;
        if (bit) {
          this.drawPixel(cursorX + x, cursorY + y, this.textColor);
        } else if (this.bgColor !== this.textColor) {
          this.drawPixel(cursorX + x, cursorY + y, this.bgColor);
        }
      }
    }
  }

  drawTextCentered(text, x, y) {
    const clipped = String(text).slice(0, CHARS_X);
    const space = Math.floor(DISPLAY_CHAR_WIDTH * (CHARS_X - clipped.length) / 2);
    this.setCursor(x + space, y);
    this.printText(clipped);
  }
}

function knobNeedleIndex(value, min, max) {
  if (max <= min) return 0;
  const clamped = Math.max(min, Math.min(max, value));
  return Math.round(((clamped - min) * 30) / (max - min));
}

function knobValueText(item) {
  if (item.ptype === "integer") return `${Math.round(item.value)}`;
  if (item.ptype === "float") return (item.value / 1000).toFixed(2);
  if (item.ptype === "text") return String(item.text?.[Math.max(0, Math.min(item.value, item.text.length - 1))] ?? `${item.value}`).toLowerCase();
  return "-";
}

function compactKnobLabel(name, value) {
  let valueText = String(value);
  if (valueText.length > 6) valueText = valueText.slice(0, 6);
  const maxName = Math.max(1, 8 - valueText.length - 1);
  const label = String(name).toLowerCase().replace(/\s+/g, "").slice(0, maxName);
  return `${label}:${valueText.toLowerCase()}`.slice(0, 8);
}

function drawKnobArrow(oled, x, y, right) {
  if (right) {
    oled.drawLine(x, y - 3, x + 3, y, WHITE);
    oled.drawLine(x + 3, y, x, y + 3, WHITE);
  } else {
    oled.drawLine(x + 3, y - 3, x, y, WHITE);
    oled.drawLine(x, y, x + 3, y + 3, WHITE);
  }
}

function drawPicopixelText(oled, text, x, y, color = WHITE) {
  let cx = x;
  for (const ch of String(text)) {
    const rows = PICOPIXEL_DIGITS[ch];
    if (!rows) {
      cx += 2;
      continue;
    }
    for (let yy = 0; yy < 5; yy += 1) {
      for (let xx = 0; xx < 3; xx += 1) {
        if (rows[yy] & (1 << (2 - xx))) oled.drawPixel(cx + xx, y + yy, color);
      }
    }
    cx += 4;
  }
}

function drawIndexLabel(oled, text) {
  const label = String(text);
  drawRect(oled, 0, 0, label.length * 4 + 3, 9, WHITE);
  drawPicopixelText(oled, label, 2, 2, WHITE);
}

function drawCVMiniBox(oled, x, label, active) {
  if (active) {
    oled.fillRect(x, 0, 7, 9, WHITE);
  } else {
    drawRect(oled, x, 0, 7, 9, WHITE);
  }
  drawPicopixelText(oled, label, x + 2, 2, active ? BLACK : WHITE);
}

function drawCVIndicator(oled, activeInput) {
  drawCVMiniBox(oled, 49, "1", activeInput === 0);
  drawCVMiniBox(oled, 57, "2", activeInput === 1);
}

function itemDisplayValue(item) {
  return item.displayValue ?? item.value;
}

function adsrDurationWidth(value, min, max) {
  const clamped = Math.max(min, Math.min(max, value));
  if (max <= min) return 3;
  return 3 + Math.floor(((clamped - min) * 13) / (max - min));
}

function drawSegmentedLine(oled, x0, y0, x1, y1, dashed) {
  let dx = Math.abs(x1 - x0);
  const sx = x0 < x1 ? 1 : -1;
  let dy = -Math.abs(y1 - y0);
  const sy = y0 < y1 ? 1 : -1;
  let err = dx + dy;
  let phase = 0;
  while (true) {
    if (!dashed || phase < 3) oled.drawPixel(x0, y0, WHITE);
    if (x0 === x1 && y0 === y1) break;
    const e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
    phase = (phase + 1) % 5;
  }
}

function drawThickLine(oled, x0, y0, x1, y1, highlight, dashed) {
  drawSegmentedLine(oled, x0, y0, x1, y1, dashed);
  if (highlight) {
    drawSegmentedLine(oled, x0, y0 - 1, x1, y1 - 1, dashed);
    drawSegmentedLine(oled, x0, y0 + 1, x1, y1 + 1, dashed);
  }
}

function drawADSRShape(oled, item) {
  const group = item.adsr;
  if (!group) return false;
  let attackW = adsrDurationWidth(itemDisplayValue(group.attack), group.attack.min, group.attack.max);
  let decayW = adsrDurationWidth(itemDisplayValue(group.decay), group.decay.min, group.decay.max);
  let releaseW = adsrDurationWidth(itemDisplayValue(group.release), group.release.min, group.release.max);
  const holdW = 8;
  const maxCurveW = 42;
  const timedW = attackW + decayW + releaseW;
  if (timedW + holdW > maxCurveW && timedW > 0) {
    const available = maxCurveW - holdW;
    attackW = Math.max(2, Math.floor((attackW * available) / timedW));
    decayW = Math.max(2, Math.floor((decayW * available) / timedW));
    releaseW = Math.max(2, available - attackW - decayW);
  }
  const curveW = attackW + decayW + holdW + releaseW;
  const sustainY = 17 - Math.floor((Math.max(0, Math.min(100, itemDisplayValue(group.sustain))) * 11) / 100);
  const x0 = Math.floor((OLED_WIDTH - curveW) / 2);
  const y0 = 19;
  const x1 = x0 + attackW;
  const y1 = 6;
  const x2 = x1 + decayW;
  const x3 = x2 + holdW;
  const x4 = x3 + releaseW;
  const dashed = group.gate.value === 0;
  drawThickLine(oled, x0, y0, x1, y1, item.adsrRole === "attack", dashed);
  drawThickLine(oled, x1, y1, x2, sustainY, item.adsrRole === "decay", dashed);
  drawThickLine(oled, x2, sustainY, x3, sustainY, item.adsrRole === "sustain", dashed);
  drawThickLine(oled, x3, sustainY, x4, y0, item.adsrRole === "release", dashed);
  oled.drawPixel(x0, y0, WHITE);
  oled.drawPixel(x1, y1, WHITE);
  oled.drawPixel(x2, sustainY, WHITE);
  oled.drawPixel(x3, sustainY, WHITE);
  oled.drawPixel(x4, y0, WHITE);
  return true;
}

function drawKnobMenu(oled, item, editing = false, navDir = 0, index = 0, total = 0, activeInput = -1) {
  const [dx, dy] = KNOB_POINTS[knobNeedleIndex(itemDisplayValue(item), item.min, item.max)];
  oled.clearDisplay();
  if (total > 0) drawIndexLabel(oled, `${index + 1}/${total}`);
  drawCVIndicator(oled, activeInput);
  drawKnobArrow(oled, KNOB_LEFT_ARROW_X + (navDir < 0 ? -2 : 0), KNOB_ARROW_Y, false);
  drawKnobArrow(oled, KNOB_RIGHT_ARROW_X + (navDir > 0 ? 2 : 0), KNOB_ARROW_Y, true);
  if (!drawADSRShape(oled, item)) {
    oled.drawCircle(KNOB_CENTER_X, KNOB_CENTER_Y, KNOB_RADIUS, WHITE);
    oled.drawPixel(KNOB_CENTER_X, KNOB_CENTER_Y, WHITE);
    oled.drawLine(KNOB_CENTER_X, KNOB_CENTER_Y, KNOB_CENTER_X + dx, KNOB_CENTER_Y + dy, WHITE);
  }
  if (editing) oled.fillRect(0, 23, OLED_WIDTH, 9, WHITE);
  oled.setTextColor(editing ? BLACK : WHITE, editing ? WHITE : BLACK);
  oled.drawTextCentered(compactKnobLabel(item.name, knobValueText(item)), 0, 24);
  oled.setTextColor(WHITE, BLACK);
}

function drawRect(oled, x, y, w, h, color = WHITE) {
  oled.drawFastHLine(x, y, w, color);
  oled.drawFastHLine(x, y + h - 1, w, color);
  oled.drawLine(x, y, x, y + h - 1, color);
  oled.drawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}

function drawCVCell(oled, x, label, selected) {
  if (selected) {
    oled.fillRect(x, 12, 24, 9, WHITE);
    oled.setTextColor(BLACK, WHITE);
  } else {
    oled.setTextColor(WHITE, BLACK);
  }
  oled.setCursor(x + 9, 13);
  oled.printText(label);
}

function cvCenteredX(x, w, text) {
  return x + Math.floor((w - text.length * 6) / 2);
}

function cvAmountText(amount) {
  if (amount === CV_VOCT_AMOUNT) return "v/oct";
  const sign = amount < 0 ? "-" : "+";
  const abs = Math.abs(amount);
  return `${sign}${Math.floor(abs / 10)}.${abs % 10}`;
}

function isFrequencyItem(item) {
  const name = (item?.name ?? "").toLowerCase();
  return name === "freq" || name === "frequency";
}

function drawCVOverlay(oled, item, state, selectedInput, amount) {
  oled.fillRect(6, 1, 52, 30, BLACK);
  drawRect(oled, 6, 1, 52, 30, WHITE);
  oled.drawLine(32, 12, 32, 20, WHITE);
  oled.setTextColor(WHITE, BLACK);
  oled.setCursor(cvCenteredX(6, 52, "cv"), 3);
  oled.printText("cv");

  drawCVCell(oled, 8, "1", selectedInput === 0);
  drawCVCell(oled, 32, "2", selectedInput === 1);

  oled.setTextColor(WHITE, BLACK);
  if (state === CV_UI_AMOUNT || state === CV_UI_WAIT_RELEASE_DONE) {
    const text = cvAmountText(amount);
    oled.setCursor(cvCenteredX(6, 52, text), 22);
    oled.printText(text);
  } else {
    const text = item.name.slice(0, 8);
    oled.setCursor(cvCenteredX(6, 52, text), 22);
    oled.printText(text);
  }
  oled.setTextColor(WHITE, BLACK);
}

function renderOledToCanvas(oled, canvas, scale) {
  const ctx = canvas.getContext("2d");
  canvas.width = OLED_WIDTH * scale;
  canvas.height = OLED_HEIGHT * scale;
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = "#fff";
  for (let y = 0; y < OLED_HEIGHT; y += 1) {
    for (let x = 0; x < OLED_WIDTH; x += 1) {
      if (oled.getPixel(x, y)) ctx.fillRect(x * scale, y * scale, scale, scale);
    }
  }
}

function drawFontPreview(font, sampleText) {
  const oled = new Oled64x32(font);
  const [name = "delay ms", value = "0.50"] = sampleText.split("|");
  oled.clearDisplay();
  oled.drawTextCentered(name, NAME_X, NAME_Y);
  oled.drawTextCentered(value, PARAMETER_X, PARAMETER_Y);
  return oled;
}

async function loadFontPreviews() {
  const grid = document.getElementById("fontGrid");
  const count = document.getElementById("fontCount");
  const sampleInput = document.getElementById("fontSample");
  const filterInput = document.getElementById("fontFilter");
  const response = await fetch("fonts.json", { cache: "no-store" });
  if (!response.ok) throw new Error(`fonts.json: HTTP ${response.status}`);
  const manifest = await response.json();
  const fonts = [{
    name: GFX_BUILTIN_FONT.name,
    current: true,
    font: GFX_BUILTIN_FONT,
  }];

  await Promise.all(manifest.fonts.map(async (entry) => {
    try {
      const font = parseFontHeader(await fetchText(entry.path));
      fonts.push({ ...entry, font });
    } catch (error) {
      fonts.push({ ...entry, error: String(error) });
    }
  }));

  fonts.sort((a, b) => {
    if (a.current !== b.current) return a.current ? -1 : 1;
    return a.name.localeCompare(b.name);
  });

  function draw() {
    const query = filterInput.value.trim().toLowerCase();
    const sample = sampleInput.value || "delay ms|0.50";
    grid.innerHTML = "";
    let shown = 0;
    for (const entry of fonts) {
      if (query && !entry.name.toLowerCase().includes(query)) continue;
      shown += 1;
      const card = document.createElement("article");
      card.className = `font-card${entry.current ? " current" : ""}`;
      const title = document.createElement("div");
      title.className = "font-card-title";
      title.textContent = entry.current ? `${entry.name} · current` : entry.name;
      card.appendChild(title);

      if (entry.error) {
        const error = document.createElement("p");
        error.className = "font-error";
        error.textContent = entry.error;
        card.appendChild(error);
      } else {
        const canvas = document.createElement("canvas");
        canvas.className = "font-preview";
        canvas.width = OLED_WIDTH * 4;
        canvas.height = OLED_HEIGHT * 4;
        card.appendChild(canvas);
        renderOledToCanvas(drawFontPreview(entry.font, sample), canvas, 4);
        const meta = document.createElement("div");
        meta.className = "font-meta";
        meta.textContent = `${entry.font.first.toString(16).toUpperCase()}–${entry.font.last.toString(16).toUpperCase()} · yAdv ${entry.font.yAdvance}`;
        card.appendChild(meta);
      }
      grid.appendChild(card);
    }
    count.textContent = `${shown}/${fonts.length} fonts`;
  }

  sampleInput.addEventListener("input", draw);
  filterInput.addEventListener("input", draw);
  draw();
}

class SelectorModel {
  constructor(oled, apps, iconFrames) {
    this.oled = oled;
    this.apps = apps;
    this.iconFrames = iconFrames;
    this.selected = 0;
    this.frame = 0;
    this.lastFrameAt = 0;
    this.drawSelected();
  }

  rotate(delta) {
    this.selected += delta;
    if (this.selected < 0) this.selected = this.apps.length - 1;
    if (this.selected >= this.apps.length) this.selected = 0;
    this.frame = 0;
    this.lastFrameAt = performance.now();
    this.drawSelected();
  }

  selectedApp() {
    return this.apps[this.selected];
  }

  drawSelected() {
    const frames = this.iconFrames[this.selected] ?? [];
    if (frames.length) {
      this.oled.setFrame(frames[this.frame % frames.length]);
    } else {
      this.oled.clearDisplay();
      this.oled.drawTextCentered(this.selectedApp().name, NAME_X, 12);
    }
  }

  tick(now) {
    const frames = this.iconFrames[this.selected] ?? [];
    if (frames.length > 1 && now - this.lastFrameAt >= SELECTOR_ANIMATION_MS) {
      this.lastFrameAt = now;
      this.frame += 1;
      this.drawSelected();
    }
  }
}

class PicoMenu {
  static PARAM_SELECT = 0;
  static PARAM_INPUT = 1;
  static WAITBUTTONRELEASE1 = 2;
  static WAITBUTTONRELEASE2 = 3;

  constructor(oled, menus) {
    this.oled = oled;
    this.menus = menus;
    this.state = PicoMenu.PARAM_SELECT;
    this.index = 0;
    this.debounceCounter = 0;
    this.encoderDelta = 0;
    this.buttonPressed = false;
    this.buttonDownAt = 0;
    this.lastClickAt = 0;
    this.buttonEvent = null;
    this.cvAssignments = menus.map(() => ({ input: 0, amount: 0, baselineVolts: 0, baselineReady: false }));
    this.cvState = CV_UI_OFF;
    this.cvEditIndex = 0;
    this.cvSelectedInput = 0;
    this.cvEditAmount = 0;
    this.cvButtonWasDown = false;
    this.lastCVDisplayAt = 0;
    this.drawMenu(0);
  }

  reset() {
    this.state = PicoMenu.PARAM_SELECT;
    this.index = 0;
    this.debounceCounter = 0;
    this.encoderDelta = 0;
    this.buttonPressed = false;
    this.buttonDownAt = 0;
    this.lastClickAt = 0;
    this.buttonEvent = null;
    this.cvState = CV_UI_OFF;
    this.cvButtonWasDown = false;
    this.lastCVDisplayAt = 0;
    for (const item of this.menus) delete item.displayValue;
    this.drawMenu(0);
  }

  rotate(delta) {
    this.encoderDelta += delta;
  }

  setButton(pressed) {
    const now = performance.now();
    if (pressed && !this.buttonPressed) {
      this.buttonDownAt = now;
    } else if (!pressed && this.buttonPressed) {
      const heldMs = now - this.buttonDownAt;
      if (heldMs >= CLICK_MIN_MS && heldMs <= CLICK_MAX_MS) {
        if (now - this.lastClickAt <= DOUBLE_CLICK_MS) {
          this.buttonEvent = "DoubleClicked";
          this.lastClickAt = 0;
        } else {
          this.buttonEvent = "Clicked";
          this.lastClickAt = now;
        }
      } else {
        this.lastClickAt = 0;
      }
      this.buttonDownAt = 0;
    }
    this.buttonPressed = pressed;
  }

  digitalReadButton() {
    return !this.buttonPressed;
  }

  getEncoderValue() {
    const value = this.encoderDelta;
    this.encoderDelta = 0;
    return value;
  }

  getButtonEvent() {
    const event = this.buttonEvent;
    this.buttonEvent = null;
    return event;
  }

  beginCVOverlay(index) {
    this.cvEditIndex = Math.max(0, Math.min(this.cvAssignments.length - 1, index));
    const assignment = this.cvAssignments[this.cvEditIndex];
    this.cvSelectedInput = assignment.input > 0 ? 1 : 0;
    this.cvEditAmount = assignment.amount;
    this.cvState = CV_UI_SELECT_INPUT;
    this.cvButtonWasDown = this.buttonPressed;
    this.drawCVOverlay();
  }

  drawCVOverlay() {
    drawCVOverlay(
      this.oled,
      this.menus[this.cvEditIndex],
      this.cvState,
      this.cvSelectedInput,
      this.cvEditAmount
    );
  }

  serviceCVOverlay(enc) {
    if (this.cvState === CV_UI_OFF) return 0;

    let dirty = false;
    const buttonDown = !this.digitalReadButton();
    const pressEdge = buttonDown && !this.cvButtonWasDown;
    this.cvButtonWasDown = buttonDown;

    if (this.cvState === CV_UI_SELECT_INPUT) {
      if (enc !== 0) {
        const nextInput = enc > 0 ? 1 : 0;
        if (this.cvSelectedInput !== nextInput) {
          this.cvSelectedInput = nextInput;
          dirty = true;
        }
      }
      if (pressEdge) {
        this.cvEditAmount = this.cvAssignments[this.cvEditIndex].amount;
        if (this.cvEditAmount === CV_VOCT_AMOUNT && !isFrequencyItem(this.menus[this.cvEditIndex])) {
          this.cvEditAmount = 0;
        }
        this.cvState = CV_UI_AMOUNT;
        dirty = true;
      }
    } else if (this.cvState === CV_UI_AMOUNT) {
      if (enc !== 0) {
        let nextAmount = this.cvEditAmount + enc;
        if (nextAmount < -10) nextAmount = -10;
        const maxAmount = isFrequencyItem(this.menus[this.cvEditIndex]) ? CV_VOCT_AMOUNT : 10;
        if (nextAmount > maxAmount) nextAmount = maxAmount;
        if (this.cvEditAmount !== nextAmount) {
          this.cvEditAmount = nextAmount;
          dirty = true;
        }
      }
      if (pressEdge) {
        this.commitCVAssignment(this.cvEditIndex, this.cvSelectedInput, this.cvEditAmount, performance.now());
        this.cvState = CV_UI_WAIT_RELEASE_DONE;
        this.cvButtonWasDown = true;
        dirty = true;
      }
    } else if (this.cvState === CV_UI_WAIT_RELEASE_DONE) {
      if (!buttonDown) {
        this.cvState = CV_UI_OFF;
        this.cvButtonWasDown = false;
        this.drawMenu(this.index, this.state === PicoMenu.PARAM_INPUT);
        return 2;
      }
    }

    if (dirty) this.drawCVOverlay();
    return 1;
  }

  drawMenu(index, editing = false) {
    drawKnobMenu(this.oled, this.menus[index], editing, 0, index, this.menus.length, this.cvActiveInput(index));
  }

  drawMenuNav(index, navDir) {
    if (navDir !== 0) {
      drawKnobMenu(this.oled, this.menus[index], false, navDir, index, this.menus.length, this.cvActiveInput(index));
      window.setTimeout(() => {
        if (this.state === PicoMenu.PARAM_SELECT) this.drawMenu(index);
      }, KNOB_NAV_ANIM_MS);
    } else {
      this.drawMenu(index);
    }
  }

  cvActiveInput(index) {
    const assignment = this.cvAssignments[index];
    return assignment && assignment.amount !== 0 ? assignment.input : -1;
  }

  cvSignalVolts(input, now, maxVolts = 3) {
    const phase = input === 0 ? 0 : Math.PI;
    const signal = (Math.sin(now * 0.002 + phase) + 1) * 0.5;
    return Math.min(maxVolts, signal * maxVolts);
  }

  cvDeltaVolts(volts, assignment, maxVolts) {
    if (!assignment.baselineReady) {
      assignment.baselineVolts = volts;
      assignment.baselineReady = true;
      return 0;
    }
    let delta = Math.max(-maxVolts, Math.min(maxVolts, volts - assignment.baselineVolts));
    if (delta > -CV_DELTA_DEADBAND_VOLTS && delta < CV_DELTA_DEADBAND_VOLTS) delta = 0;
    return delta;
  }

  commitCVAssignment(index, input, amount, now) {
    const item = this.menus[index];
    if (amount === CV_VOCT_AMOUNT && !isFrequencyItem(item)) amount = 0;
    const maxVolts = amount === CV_VOCT_AMOUNT && isFrequencyItem(item) ? 8 : 3;
    this.cvAssignments[index] = {
      input,
      amount,
      baselineVolts: amount === 0 ? 0 : this.cvSignalVolts(input, now, maxVolts),
      baselineReady: amount !== 0,
    };
    delete item.displayValue;
  }

  updateCVDisplay(now) {
    let dirty = false;
    for (let index = 0; index < this.menus.length; index += 1) {
      const item = this.menus[index];
      const assignment = this.cvAssignments[index];
      if (!assignment || assignment.amount === 0 || item.max <= item.min) {
        if (item.displayValue !== undefined) {
          delete item.displayValue;
          dirty = true;
        }
        continue;
      }

      let next;
      if (assignment.amount === CV_VOCT_AMOUNT && isFrequencyItem(item)) {
        const volts = this.cvSignalVolts(assignment.input, now, 8);
        const deltaVolts = this.cvDeltaVolts(volts, assignment, 8);
        next = Math.round(item.value * Math.pow(2, deltaVolts));
      } else {
        const range = item.max - item.min;
        const volts = this.cvSignalVolts(assignment.input, now, 3);
        const deltaVolts = this.cvDeltaVolts(volts, assignment, 3);
        const delta = Math.round(deltaVolts * (assignment.amount / 10) * range / 3);
        next = item.value + delta;
      }
      next = Math.max(item.min, Math.min(item.max, next));
      if (item.displayValue !== next) {
        item.displayValue = next;
        dirty = true;
      }
    }
    return dirty;
  }

  tick() {
    const now = performance.now();
    const enc = this.getEncoderValue();
    const buttonEvent = this.getButtonEvent();

    if (this.serviceCVOverlay(enc) !== 0) return;

    if (buttonEvent === "DoubleClicked") {
      this.beginCVOverlay(this.index);
      return;
    }

    switch (this.state) {
      case PicoMenu.PARAM_SELECT:
        if (enc !== 0) {
          this.index += enc;
          if (this.index < 0) this.index = this.menus.length - 1;
          if (this.index > this.menus.length - 1) this.index = 0;
          this.drawMenuNav(this.index, enc);
        }
        if (!this.digitalReadButton()) {
          this.drawMenu(this.index, true);
          this.state = PicoMenu.WAITBUTTONRELEASE1;
          this.debounceCounter = MENU_RELEASE_DEBOUNCE_CYCLES;
        }
        break;
      case PicoMenu.WAITBUTTONRELEASE1:
        if (this.digitalReadButton()) {
          this.debounceCounter -= 1;
          if (this.debounceCounter <= 0) {
            this.state = PicoMenu.PARAM_INPUT;
            this.drawMenu(this.index, true);
          }
        }
        break;
      case PicoMenu.PARAM_INPUT:
        if (enc !== 0) {
          const item = this.menus[this.index];
          item.value += enc * item.step;
          item.value = Math.max(item.min, Math.min(item.max, item.value));
          this.drawMenu(this.index, true);
        }
        if (!this.digitalReadButton()) {
          this.debounceCounter = MENU_RELEASE_DEBOUNCE_CYCLES;
          this.state = PicoMenu.WAITBUTTONRELEASE2;
          this.drawMenu(this.index, false);
        }
        break;
      case PicoMenu.WAITBUTTONRELEASE2:
        if (this.digitalReadButton()) {
          this.debounceCounter -= 1;
          if (this.debounceCounter <= 0) this.state = PicoMenu.PARAM_SELECT;
        }
        break;
      default:
        this.state = PicoMenu.PARAM_SELECT;
        break;
    }

    if (this.updateCVDisplay(now) && now - this.lastCVDisplayAt >= CV_DISPLAY_INTERVAL_MS) {
      this.lastCVDisplayAt = now;
      this.drawMenu(this.index, this.state === PicoMenu.PARAM_INPUT);
    }
  }
}

class PlaceholderApp {
  constructor(oled, appName) {
    this.oled = oled;
    this.appName = appName;
    this.draw();
  }

  reset() {
    this.draw();
  }

  rotate() {
  }

  setButton() {
  }

  tick() {
  }

  draw() {
    this.oled.clearDisplay();
    this.oled.drawTextCentered("coming", NAME_X, 8);
    this.oled.drawTextCentered("soon...", NAME_X, 16);
  }
}

class WavetableMenu extends PicoMenu {
  constructor(oled, menus) {
    super(oled, menus);
    this.cvAssignments[2] = { input: 1, amount: CV_VOCT_AMOUNT, baselineVolts: 0, baselineReady: false };
  }

  drawMenu(index, editing = false) {
    if (index !== 1) {
      super.drawMenu(index, editing);
      return;
    }
    this.drawWaveView(editing);
  }

  drawWaveView(editing) {
    const layers = 5;
    const points = 24;
    const centerWave = this.menus[1].displayValue ?? this.menus[1].value;
    const first = [];
    const last = [];
    this.oled.clearDisplay();

    for (let layer = 0; layer < layers; layer += 1) {
      const rel = layer - Math.floor(layers / 2);
      const wave = Math.max(0, Math.min(this.menus[1].max, centerWave + rel));
      const depth = layers - 1 - layer;
      const xOffset = depth * 2;
      const baseY = 4 + layer * 3;
      let previous = null;
      for (let point = 0; point < points; point += 1) {
        const phase = point / points;
        const harmonic = 1 + (wave % 5);
        const sample = Math.sin(phase * Math.PI * 2) * 2.5 +
          Math.sin(phase * Math.PI * 2 * harmonic) * 0.8;
        const current = {
          x: 5 + xOffset + point * 2,
          y: Math.max(0, Math.min(19, Math.round(baseY - sample))),
        };
        if (point === 0) first[layer] = current;
        if (previous) {
          if (rel === 0) {
            this.oled.drawLine(previous.x, previous.y, current.x, current.y, WHITE);
            this.oled.drawLine(previous.x, previous.y + 1, current.x, current.y + 1, WHITE);
          } else if ((point & 1) !== 0) {
            this.oled.drawLine(previous.x, previous.y, current.x, current.y, WHITE);
          }
        }
        if (point === points - 1) last[layer] = current;
        previous = current;
      }
    }

    for (let layer = 1; layer < layers; layer += 1) {
      this.oled.drawLine(first[layer - 1].x, first[layer - 1].y, first[layer].x, first[layer].y, WHITE);
      this.oled.drawLine(last[layer - 1].x, last[layer - 1].y, last[layer].x, last[layer].y, WHITE);
    }

    if (editing) this.oled.fillRect(0, 23, OLED_WIDTH, 9, WHITE);
    this.oled.setTextColor(editing ? BLACK : WHITE, editing ? WHITE : BLACK);
    this.oled.drawTextCentered(`wav ${centerWave}`, 0, 24);
    this.oled.setTextColor(WHITE, BLACK);
  }
}

function delayMenu() {
  return [
    { name: "delay ms", min: 0, max: 1000, step: 10, ptype: "integer", value: 500 },
    { name: "fdback", min: 0, max: 1000, step: 10, ptype: "float", value: 100 },
    { name: "x fdback", min: 0, max: 1000, step: 10, ptype: "float", value: 100 },
    { name: "mix", min: 0, max: 1000, step: 10, ptype: "float", value: 100 },
    { name: "level", min: 0, max: 1000, step: 10, ptype: "float", value: 800 },
  ];
}

function reverbMenu() {
  return [
    { name: "feedback", min: 0, max: 100, step: 1, ptype: "integer", value: 50 },
    { name: "cutoff", min: 550, max: 44100 / 2, step: 500, ptype: "integer", value: 5050 },
    { name: "mix", min: 0, max: 100, step: 1, ptype: "integer", value: 50 },
    { name: "level", min: 0, max: 100, step: 1, ptype: "integer", value: 75 },
  ];
}

function wavetableMenu() {
  const gate = { name: "gate", min: 0, max: 2, step: 1, ptype: "text", value: 1, text: ["off", "cv1", "cv2"], adsrRole: "on" };
  const attack = { name: "atk", min: 0, max: 100, step: 1, ptype: "integer", value: 1, adsrRole: "attack" };
  const decay = { name: "dec", min: 0, max: 100, step: 1, ptype: "integer", value: 15, adsrRole: "decay" };
  const sustain = { name: "sus", min: 0, max: 100, step: 1, ptype: "integer", value: 80, adsrRole: "sustain" };
  const release = { name: "rel", min: 0, max: 100, step: 1, ptype: "integer", value: 10, adsrRole: "release" };
  const group = { gate, attack, decay, sustain, release };
  for (const item of [gate, attack, decay, sustain, release]) item.adsr = group;
  return [
    { name: "bank", min: 0, max: 1, step: 1, ptype: "text", value: 0, text: ["waves", "basic"] },
    { name: "wave", min: 0, max: 165, step: 1, ptype: "integer", value: 0 },
    { name: "freq", min: 20, max: 2000, step: 1, ptype: "integer", value: 90 },
    { name: "level", min: 0, max: 100, step: 1, ptype: "integer", value: 75 },
    gate,
    attack,
    decay,
    sustain,
    release,
  ];
}

function appModelFor(oled, app) {
  if (app.id === "delay") return new PicoMenu(oled, delayMenu());
  if (app.id === "reverb") return new PicoMenu(oled, reverbMenu());
  if (app.id === "wavetable") return new WavetableMenu(oled, wavetableMenu());
  return new PlaceholderApp(oled, app.name);
}

class Simulator {
  constructor(font, apps, iconFrames) {
    this.canvas = document.getElementById("oled");
    this.ctx = this.canvas.getContext("2d");
    this.status = document.getElementById("status");
    this.appSelect = document.getElementById("appSelect");
    this.scaleInput = document.getElementById("scaleInput");
    this.scaleValue = document.getElementById("scaleValue");
    this.scale = Number(this.scaleInput.value);
    this.oled = new Oled64x32(font);
    this.apps = apps;
    this.iconFrames = iconFrames;
    this.buttonDownAt = null;
    this.longPressFired = false;
    this.mode = "selector";
    this.selector = new SelectorModel(this.oled, apps, iconFrames);
    this.currentApp = null;
    this.currentAppInfo = null;
    this.populateAppSelect();
    this.applyScale();
    this.bindEvents();
    this.setStatus("selector ready");
    this.loop();
  }

  populateAppSelect() {
    this.appSelect.innerHTML = "";
    this.apps.forEach((app, index) => {
      const option = document.createElement("option");
      option.value = String(index);
      option.textContent = `${app.slot}: ${app.name}`;
      this.appSelect.appendChild(option);
    });
  }

  bindEvents() {
    window.addEventListener("keydown", (event) => {
      if (["ArrowLeft", "ArrowRight", "ArrowDown"].includes(event.key)) {
        event.preventDefault();
      }
      if (event.repeat && event.key === "ArrowDown") return;
      if (event.key === "ArrowLeft") {
        this.rotate(-1);
      } else if (event.key === "ArrowRight") {
        this.rotate(1);
      } else if (event.key === "ArrowDown") {
        this.pressButton();
      } else if (event.key.toLowerCase() === "s") {
        this.enterSelector("manual selector");
      } else if (event.key.toLowerCase() === "r") {
        this.resetCurrent();
      } else if (/^[1-9]$/.test(event.key)) {
        const index = Number(event.key) - 1;
        if (index < this.apps.length) this.enterApp(index);
      }
    });
    window.addEventListener("keyup", (event) => {
      if (["ArrowLeft", "ArrowRight", "ArrowDown"].includes(event.key)) {
        event.preventDefault();
      }
      if (event.key === "ArrowDown") {
        this.releaseButton();
      }
    });
    this.appSelect.addEventListener("change", () => {
      this.selector.selected = Number(this.appSelect.value);
      this.selector.frame = 0;
      this.selector.drawSelected();
      this.setStatus("selector jump");
    });
    document.getElementById("enterButton").addEventListener("click", () => this.enterSelectedApp());
    document.getElementById("selectorButton").addEventListener("click", () => this.enterSelector("manual selector"));
    document.getElementById("resetButton").addEventListener("click", () => this.resetCurrent());
    this.scaleInput.addEventListener("input", () => {
      this.scale = Number(this.scaleInput.value);
      this.applyScale();
      this.render();
    });
  }

  applyScale() {
    this.scaleValue.textContent = `${this.scale}×`;
    this.canvas.width = OLED_WIDTH * this.scale;
    this.canvas.height = OLED_HEIGHT * this.scale;
    this.canvas.style.width = `${OLED_WIDTH * this.scale}px`;
    this.canvas.style.height = `${OLED_HEIGHT * this.scale}px`;
  }

  rotate(delta) {
    if (this.mode === "selector") {
      this.selector.rotate(delta);
      this.appSelect.value = String(this.selector.selected);
      this.setStatus(delta < 0 ? "selector left" : "selector right");
      return;
    }
    this.currentApp.rotate(delta);
    this.setStatus(delta < 0 ? "encoder left" : "encoder right");
  }

  pressButton() {
    if (this.buttonDownAt !== null) return;
    this.buttonDownAt = performance.now();
    this.longPressFired = false;
    if (this.mode === "selector") {
      this.setStatus("selector button down");
      return;
    }
    this.currentApp.setButton(true);
    this.setStatus("button down");
  }

  releaseButton() {
    if (this.mode === "selector" && this.buttonDownAt !== null && !this.longPressFired) {
      this.enterSelectedApp();
    }
    if (this.mode === "app" && this.currentApp) this.currentApp.setButton(false);
    this.buttonDownAt = null;
    this.longPressFired = false;
    this.setStatus("button up");
  }

  enterSelectedApp() {
    this.enterApp(this.selector.selected);
  }

  enterApp(index) {
    const app = this.apps[index];
    if (!app) return;
    this.mode = "app";
    this.currentAppInfo = app;
    this.currentApp = appModelFor(this.oled, app);
    this.setStatus(`entered ${app.name}`);
  }

  enterSelector(eventText) {
    this.mode = "selector";
    this.currentApp = null;
    this.currentAppInfo = null;
    this.buttonDownAt = null;
    this.longPressFired = false;
    this.selector.drawSelected();
    this.setStatus(eventText);
  }

  resetCurrent() {
    if (this.mode === "selector") {
      this.selector.frame = 0;
      this.selector.drawSelected();
      this.setStatus("selector reset");
    } else if (this.currentApp) {
      this.currentApp.reset();
      this.setStatus("app reset");
    }
  }

  setStatus(eventText) {
    const app = this.mode === "selector" ? this.selector.selectedApp() : this.currentAppInfo;
    let detail = "";
    if (this.mode === "app" && this.currentApp instanceof PicoMenu) {
      const states = ["PARAM_SELECT", "PARAM_INPUT", "WAIT_RELEASE1", "WAIT_RELEASE2"];
      const cvStates = ["off", "cv input", "cv amount", "cv done"];
      const cv = this.currentApp.cvState === CV_UI_OFF
        ? ""
        : ` | ${cvStates[this.currentApp.cvState]} cv${this.currentApp.cvSelectedInput + 1} amt=${cvAmountText(this.currentApp.cvEditAmount)}`;
      detail = ` | ${states[this.currentApp.state] ?? "?"} | item=${this.currentApp.index + 1}/${this.currentApp.menus.length}${cv}`;
    }
    this.status.textContent = `${this.mode.toUpperCase()} | ${app?.name ?? "?"}${detail}\n` +
      `event=${eventText}`;
  }

  render() {
    this.ctx.fillStyle = "#000";
    this.ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
    for (let y = 0; y < OLED_HEIGHT; y += 1) {
      for (let x = 0; x < OLED_WIDTH; x += 1) {
        this.ctx.fillStyle = this.oled.getPixel(x, y) ? "#fff" : "#000";
        this.ctx.fillRect(x * this.scale, y * this.scale, this.scale, this.scale);
      }
    }
  }

  loop() {
    const now = performance.now();
    if (this.buttonDownAt !== null && !this.longPressFired) {
      const heldMs = now - this.buttonDownAt;
      if (heldMs >= LONG_PRESS_MS) {
        this.longPressFired = true;
        if (this.mode === "app") {
          this.enterSelector("LONG PRESS -> selector");
        } else {
          this.setStatus("selector long press");
        }
      }
    }

    if (this.mode === "selector") {
      this.selector.tick(now);
    } else if (this.currentApp) {
      this.currentApp.tick(now);
    }
    this.render();
    window.setTimeout(() => this.loop(), LOOP_MS);
  }
}

async function fetchText(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) throw new Error(`${path}: HTTP ${response.status}`);
  return response.text();
}

async function main() {
  const status = document.getElementById("status");
  try {
    const [appsManifest, iconsText] = await Promise.all([
      fetch("../Bootloader/apps.json", { cache: "no-store" }).then((response) => {
        if (!response.ok) throw new Error(`apps.json: HTTP ${response.status}`);
        return response.json();
      }),
      fetchText("../Bootloader/Selector/selector_app_icons.h"),
    ]);
    const apps = [...appsManifest.apps].sort((a, b) => a.slot - b.slot);
    new Simulator(GFX_BUILTIN_FONT, apps, parseSelectorIcons(iconsText));
    loadFontPreviews().catch((error) => {
      document.getElementById("fontCount").textContent = "Font preview failed";
      document.getElementById("fontGrid").textContent = String(error);
    });
  } catch (error) {
    status.textContent = `Failed to load simulator assets.\nRun from repo root with:\n  python3 sims/serve.py\n\n${error}`;
  }
}

main();
