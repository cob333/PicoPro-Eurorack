#include "TunerUI.h"
#include <math.h>
void TunerUI::begin(bool display_ok, bool audio_ok) {
  available_ = display_ok; audio_ok_ = audio_ok;
  if (available_) { display_.setFont(nullptr); display_.setTextColor(SSD1306_WHITE); }
}
void TunerUI::update(const TunerReading &reading, uint32_t now) {
  reading_ = reading;
  if (reading.valid) {
    // Equivalent +/-0.08-octave bounds without a logarithm per result.
    if (smooth_frequency_ <= 0 || reading.frequency > smooth_frequency_ * 1.05701804f ||
        reading.frequency < smooth_frequency_ * 0.94605765f)
      smooth_frequency_ = reading.frequency;
    else smooth_frequency_ += 0.35f * (reading.frequency - smooth_frequency_);
    reading_.frequency = smooth_frequency_;
  } else smooth_frequency_ = 0;
  updated_ms_ = now;
  dirty_ = true;
}
void TunerUI::service(uint32_t now) {
  if (reading_.valid && now - updated_ms_ > 700u) { reading_.valid = false; smooth_frequency_ = 0; dirty_ = true; }
  if (!available_ || !dirty_ || now - drawn_ms_ < 80u) return;
  if (draw()) display_.display();
  dirty_ = false; drawn_ms_ = now;
}
bool TunerUI::draw() {
  if (!reading_.valid) {
    if (last_note_ == -1) return false;
    last_note_ = -1;
    display_.clearDisplay(); display_.setTextSize(1);
    if (inverted_) { display_.invertDisplay(false); inverted_ = false; }
    if (audio_ok_) {
      // Same size-2 font as the note; split to fit the 64x32 display.
      display_.setTextSize(2);
      display_.setCursor(20, 0); display_.print("no");
      // Six size-2 glyphs fit with ten-pixel advance (no extra cell gap).
      static const char label[] = "signal";
      for (uint8_t i = 0; i < 6; ++i) {
        display_.setCursor(2 + i * 10, 16); display_.write(label[i]);
      }
    } else {
      display_.setCursor(5, 8); display_.print("I2S error");
    }
    return true;
  }
  static const char *const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  const float midi = 69 + 12 * log2f(reading_.frequency / 440);
  const int note = static_cast<int>(lroundf(midi));
  const int cents = static_cast<int>(lroundf((midi - note) * 100));
  const int hz = static_cast<int>(lroundf(reading_.frequency));
  if (note == last_note_ && cents == last_cents_ && hz == last_hz_) return false;
  last_note_ = note; last_cents_ = cents; last_hz_ = hz;
  display_.clearDisplay();
  // Match the displayed (rounded) cents, rather than exact float equality.
  const bool highlight = cents == 0;
  if (highlight != inverted_) { display_.invertDisplay(highlight); inverted_ = highlight; }
  display_.setTextSize(2); display_.setCursor(2, 2);
  display_.print(names[note % 12]); display_.print(note / 12 - 1);
  display_.setTextSize(1); display_.setCursor(45, 4);
  display_.write(cents < 0 ? '-' : '+'); display_.print(cents < 0 ? -cents : cents);
  display_.drawFastHLine(2, 19, 60, SSD1306_WHITE); display_.drawFastVLine(32, 17, 5, SSD1306_WHITE);
  int x = 32 + cents * 29 / 50; if (x < 3) x = 3; if (x > 61) x = 61;
  display_.fillRect(x - 1, 17, 3, 5, SSD1306_WHITE);
  // Default size-1 font has a fixed six-pixel cell; range is 32..2010 Hz.
  const int digits = hz >= 1000 ? 4 : hz >= 100 ? 3 : 2;
  display_.setCursor((display_.width() - (digits + 2) * 6) / 2, 24);
  display_.print(hz); display_.print("Hz");
  return true;
}
