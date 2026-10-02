// Real Adafruit default/Picopixel glyphs; no simulator substitute font.
#include <Adafruit_GFX.h>
#include <glcdfont.c>
#include "../../../Fonts/Picopixel.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>

static bool content[32][64], controls[32][64];
static bool clipBottom = false;
static void pixel(bool target[32][64], int x, int y) {
  // SSD1306 clips the old y25 status descender at the lower screen edge.
  if (clipBottom && y == 32) return;
  assert(x >= 0 && x < 64 && y >= 0 && y < 32);
  target[y][x] = true;
}
static void line(int x0, int y0, int x1, int y1) {
  int dx = std::abs(x1-x0), sx = x0<x1 ? 1:-1;
  int dy = -std::abs(y1-y0), sy = y0<y1 ? 1:-1, err = dx+dy;
  for (;;) {
    pixel(controls,x0,y0);
    if (x0 == x1 && y0 == y1) break;
    int e = 2*err;
    if (e >= dy) { err += dy; x0 += sx; }
    if (e <= dx) { err += dx; y0 += sy; }
  }
}
static int tinyWidth(const char *s) {
  int x = 0, right = 0;
  for (; *s; ++s) {
    const auto &g = Picopixel.glyph[(uint8_t)*s - Picopixel.first];
    right = x + g.xOffset + g.width;
    x += g.xAdvance;
  }
  return right;
}
static void text(const char *s, int y, bool tiny = false, int scale = 1) {
  int n = strlen(s), pitch = n > 10 ? 5 : 6;
  int x = (64 - (tiny ? tinyWidth(s) : n*pitch*scale))/2;
  for (; *s; ++s) {
    if (tiny) {
      const auto &g = Picopixel.glyph[(uint8_t)*s - Picopixel.first];
      for (int row = 0; row < g.height; ++row) for (int col = 0; col < g.width; ++col) {
        int bit = row*g.width+col;
        if (Picopixel.bitmap[g.bitmapOffset+bit/8] & (0x80 >> (bit%8)))
          pixel(content, x+g.xOffset+col, y+g.yOffset+row);
      }
      x += g.xAdvance;
    } else {
      for (int col = 0; col < 5; ++col) for (int row = 0; row < 8; ++row) {
        if (font[(uint8_t)*s*5+col] & (1 << row))
          for (int a = 0; a < scale; ++a) for (int b = 0; b < scale; ++b)
            pixel(content,x+col*scale+a,y+row*scale+b);
      }
      x += pitch*scale;
    }
  }
}
static void begin(bool back = false, int indexWidth = 15) {
  memset(content,0,sizeof(content)); memset(controls,0,sizeof(controls));
  if (indexWidth) {
    line(0,0,indexWidth-1,0); line(0,8,indexWidth-1,8);
    line(0,0,0,8); line(indexWidth-1,0,indexWidth-1,8);
  }
  // Page index reserves its whole box, not merely its border.
  for (int y = 0; y < 9; ++y) for (int x = 0; x < indexWidth; ++x) controls[y][x] = true;
  if (!back) { line(7,13,4,16); line(4,16,7,19); line(56,13,59,16); line(59,16,56,19); }
}
static void verify(const char *footer) {
  for (int y = 0; y < 32; ++y) for (int x = 0; x < 64; ++x) {
    if (content[y][x] && controls[y][x]) fprintf(stderr,"Collision %s at %d,%d\n",footer,x,y);
    assert(!(content[y][x] && controls[y][x]));
    if (y >= 23) assert(!content[y][x]); // Content may not enter footer band.
  }
  memset(content,0,sizeof(content)); text(footer,24);
  for (int y = 0; y < 24; ++y) for (int x = 0; x < 64; ++x) assert(!content[y][x]);
  // Pressed footer inversion changes colors only, not glyph positions.
}
int main() {
  begin(); text("stereo",10); verify("routing");
  begin(); text("mono*2",10); verify("routing");
  begin(); text("normal",10); verify("rotation");
  begin(); text("reverse",10); verify("rotation");
  begin(); text("process",10); verify("calibration");
  begin(false,0); text("Are you",0); text("sure?",9); verify("no");
  for (int step = 0; step < 8; ++step) {
    char label[12]; snprintf(label,sizeof(label),"cv%d %dv",step/4+1,step%4);
    memset(content,0,sizeof(content)); memset(controls,0,sizeof(controls));
    text(label,0); text("adc 4095",10); text("press set",18);
    snprintf(label,sizeof(label),"set c%d %d",step/4+1,step%4);
    text(label,25);
  }
  const char *statuses[] = {"ready","need all","invalid cal","saved","save err"};
  for (const auto status : statuses) {
    memset(content,0,sizeof(content)); memset(controls,0,sizeof(controls));
    text("save",0); text("c1 1200",10); text("c2 1200",18);
    clipBottom = true; text(status,25); clipBottom = false;
  }
  puts("Settings root layout and original Calibration real-glyph layout passed");
}
