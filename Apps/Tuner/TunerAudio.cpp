#include "TunerAudio.h"
#include <PicoSystemRuntime.h>
#include <string.h>
void TunerAudio::service(I2S &audio) {
  // Preserve partial stereo transfers and retain a pending pair under output
  // backpressure. Never discard a channel or block waiting for DMA space.
  for (uint8_t count = 0; count < 32; ++count) {
    if (received_ < 8) {
      if (audio.available() < 4) break;
      received_ += audio.read(reinterpret_cast<uint8_t *>(pair_) + received_, 8 - received_);
      if (received_ != 8) continue;
    }
    if (transmitted_ == 0) PicoProRouteAudioInput(pair_[0], pair_[1]);
    transmitted_ += audio.write(reinterpret_cast<const uint8_t *>(pair_) + transmitted_, 8 - transmitted_);
    if (transmitted_ != 8) break;
    received_ = transmitted_ = 0;
    float value;
    if (!frontend_.process(pair_[0] * (1.0f / 2147483648.0f), &value)) continue;
    history_[index_++] = value;
    if (index_ == TunerDetector::kFrame) index_ = 0;
    if (collected_ < TunerDetector::kFrame) {
      if (++collected_ == TunerDetector::kFrame) publish();
    } else if (++hop_ == kHop) {
      hop_ = 0;
      publish();
    }
  }
}
void TunerAudio::publish() {
  for (uint8_t next = 0; next < 3; ++next) {
    uint8_t expected = FREE;
    if (!__atomic_compare_exchange_n(&state_[next], &expected, FILL, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) continue;
    // Bounded snapshot of the latest chronological window. Reader-owned
    // buffers are never modified, even when analysis falls behind.
    memcpy(frames_[next], history_ + index_, (TunerDetector::kFrame - index_) * sizeof(float));
    memcpy(frames_[next] + TunerDetector::kFrame - index_, history_, index_ * sizeof(float));
    __atomic_store_n(&state_[next], READY, __ATOMIC_RELEASE);
    return;
  }
  __atomic_fetch_add(&dropped_, 1u, __ATOMIC_RELAXED);
}
const float *TunerAudio::acquire() {
  for (uint8_t i = 0; i < 3; ++i) {
    uint8_t expected = READY;
    if (__atomic_compare_exchange_n(&state_[i], &expected, READING, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return frames_[i];
  }
  return nullptr;
}
void TunerAudio::release(const float *frame) {
  for (uint8_t i = 0; i < 3; ++i) if (frame == frames_[i]) {
    __atomic_store_n(&state_[i], FREE, __ATOMIC_RELEASE);
    return;
  }
}
