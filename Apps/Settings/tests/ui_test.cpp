#include "../SettingsUI.h"
#include <PicoSystemRuntime.h>
#include <cassert>
#include <cstdio>

PicoBootSystemSettings runtimeSettings = {0,0};
static PicoBootSystemSettings stored = {0,0};
static unsigned writes = 0, captures = 0, configReads = 0;
static bool saveOK = true;
static uint32_t now = 0;
int PicoBootLoadSystemSettings(PicoBootSystemSettings *out) { ++configReads; *out = stored; return 1; }
int PicoBootSaveSystemSettings(const PicoBootSystemSettings *s) {
  ++writes;
  if (!saveOK) return 0;
  stored = *s; return 1;
}
int PicoBootLoadCalibration(PicoBootCalibration *out) {
  *out = {580,590,5456,3250,3260,123,{7,8}}; return 1;
}
int PicoBootSaveCalibration(const PicoBootCalibration *) { assert(false); return 0; }
int PicoBootCVCalibrationValid(const PicoBootCalibration *) { return 1; }
uint16_t PicoCVInputReadRawFresh(uint8_t) { ++captures; return 3250; }
void delay(uint32_t) {}
static void click(SettingsUI &ui) {
  ui.service(0,true,now += 10); ui.service(0,true,now += 20);
  ui.service(0,false,now += 10); ui.service(0,false,now += 20);
}
static void rotate(SettingsUI &ui, int delta) { ui.service(delta,false,now += 10); }
int main() {
  Adafruit_SSD1306 d; SettingsUI ui(d);
  ui.begin(true,false,now); ui.service(0,false,now);
  assert(d.text.find("stereo") != std::string::npos);
  const auto initialUpdates = d.updates;
  ui.service(0,false,now += 1000);
  assert(d.updates == initialUpdates); // Static pages do not redraw while idle.
  click(ui); rotate(ui,1);
  assert(d.text.find("mono*2") != std::string::npos && writes == 0);
  click(ui); assert(writes == 1 && stored.audio_routing == 1 && runtimeSettings.audio_routing == 1);
  rotate(ui,1); click(ui); rotate(ui,1);
  assert(writes == 1 && d.rotation == 0); // Draft rotation is not applied yet.
  saveOK = false; click(ui);
  assert(d.text.find("save err") != std::string::npos && stored.screen_rotation == 0 && d.rotation == 0);
  click(ui); assert(d.text.find("normal") != std::string::npos); // Roll back failed draft.
  rotate(ui,1); saveOK = true; click(ui);
  assert(writes == 3 && stored.screen_rotation == 1 && d.rotation == 2);
  rotate(ui,1); click(ui);
  assert(d.text.find("Are yousure?") != std::string::npos && d.text.find("no") != std::string::npos);
  const auto confirmationUpdates = d.updates;
  ui.service(0,false,now += 1000);
  assert(d.updates == confirmationUpdates);
  click(ui); assert(d.text.find("process") != std::string::npos && captures == 0);
  click(ui); rotate(ui,1); click(ui);
  assert(d.text.find("cv1 0v") != std::string::npos && captures == 1);
  ui.service(0,false,now += 120);
  assert(captures == 2); // Only live ADC pages retain periodic updates.
  const auto reads = captures;
  ui.service(0,false,now += 20); assert(captures == reads); // No click-release leak.
  click(ui);
  assert(d.text.find("cv1 1v") != std::string::npos && d.text.find("set c1 0") != std::string::npos);
  rotate(ui,7); click(ui);
  assert(d.text.find("need all") != std::string::npos);
  const auto saveUpdates = d.updates, saveReads = captures;
  ui.service(0,false,now += 1000);
  assert(d.updates == saveUpdates && captures == saveReads);

  Adafruit_SSD1306 cachedDisplay; SettingsUI cachedUI(cachedDisplay);
  runtimeSettings = {1,1}; stored = {0,0};
  cachedUI.begin(true,false,now); cachedUI.service(0,false,now);
  assert(cachedDisplay.rotation == 2 && cachedDisplay.text.find("mono*2") != std::string::npos);
  assert(configReads == 0); // UI reuses startup cache rather than reading Flash.
  puts("Settings editing, persistence failure, rotation and confirmation UI regression passed");
}
