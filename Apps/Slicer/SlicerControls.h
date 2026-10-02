#ifndef PICOPRO_SLICER_CONTROLS_H
#define PICOPRO_SLICER_CONTROLS_H
#include "SlicerEngine.h"
#include <string.h>
#include <type_traits>

struct SlicerControlPacket {
  SlicerParameters parameters;
  uint32_t clockEpoch = 0;
};

// One core0 writer, one core1 reader. Atomic payload words avoid C++ data races;
// bounded seqlock retries retain the previous snapshot on contention.
class SlicerControls {
 public:
  void publish(const SlicerControlPacket &packet) {
    uint32_t next[kWords] = {};
    memcpy(next, &packet, sizeof(packet));
    bool changed = false;
    for (uint8_t i = 0; i < kWords; ++i) {
      if (__atomic_load_n(&words_[i], __ATOMIC_RELAXED) != next[i]) { changed = true; break; }
    }
    if (!changed) return; // Stable controls do not invalidate the audio snapshot.
    __atomic_add_fetch(&revision_, 1u, __ATOMIC_ACQ_REL);
    for (uint8_t i = 0; i < kWords; ++i) __atomic_store_n(&words_[i], next[i], __ATOMIC_RELAXED);
    __atomic_add_fetch(&revision_, 1u, __ATOMIC_RELEASE);
  }
  bool read(SlicerControlPacket &packet, uint32_t &lastRevision) const {
    for (uint8_t attempt = 0; attempt < 4; ++attempt) {
      const uint32_t before = __atomic_load_n(&revision_, __ATOMIC_ACQUIRE);
      if (before & 1u) continue;
      if (before == lastRevision) return false;
      uint32_t next[kWords];
      for (uint8_t i = 0; i < kWords; ++i) next[i] = __atomic_load_n(&words_[i], __ATOMIC_RELAXED);
      // Full fence keeps payload loads before the second revision check.
      __atomic_thread_fence(__ATOMIC_ACQ_REL);
      if (before != __atomic_load_n(&revision_, __ATOMIC_ACQUIRE)) continue;
      memcpy(&packet, next, sizeof(packet));
      lastRevision = before;
      return true;
    }
    return false;
  }
  void mute() { __atomic_store_n(&muted_, true, __ATOMIC_RELEASE); }
  bool muted() const { return __atomic_load_n(&muted_, __ATOMIC_ACQUIRE); }
 private:
  static_assert(std::is_trivially_copyable<SlicerControlPacket>::value, "Control packets must be fixed snapshots");
  static constexpr uint8_t kWords = (sizeof(SlicerControlPacket) + 3) / 4;
  uint32_t words_[kWords] = {};
  uint32_t revision_ = 0;
  bool muted_ = false;
};
extern SlicerControls slicerControls;
#endif
