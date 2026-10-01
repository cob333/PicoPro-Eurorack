#include "AcidState.h"

#include "AcidCV.h"
#include "PicoAppState.h"

namespace {

constexpr uint32_t kStateTagV2 = PICOPRO_APP_STATE_TAG('A', 'C', 'S', '2');
constexpr uint32_t kStateTagV3 = PICOPRO_APP_STATE_TAG('A', 'C', 'S', '3');

struct AcidPersistentStateV2 {
  uint8_t version;
  AcidSequenceConfig sequence;
  AcidStep steps[AcidSequencer::kMaxSteps];
  AcidBassConfig bass;
};

struct AcidPersistentStateV3 {
  uint8_t version;
  AcidSequenceConfig sequence;
  AcidStep steps[AcidSequencer::kMaxSteps];
  AcidBassConfig bass;
  PicoCVPersistentState cv;
};

static_assert(sizeof(AcidPersistentStateV2) <= PICOPRO_APP_STATE_PAYLOAD_BYTES,
              "Acid state exceeds PicoAppState payload");
static_assert(sizeof(AcidPersistentStateV3) <= PICOPRO_APP_STATE_PAYLOAD_BYTES,
              "Acid state with CV exceeds PicoAppState payload");

}  // namespace

bool AcidLoadState(AcidSequencer &sequencer, AcidBass &bass) {
  AcidPersistentStateV3 state_v3;
  if (PicoAppStateLoad(kStateTagV3, &state_v3, sizeof(state_v3)) &&
      state_v3.version == 3u) {
    sequencer.restoreState(state_v3.sequence, state_v3.steps);
    bass.restoreConfig(state_v3.bass);
    AcidCVImportState(&state_v3.cv);
    return true;
  }

  AcidPersistentStateV2 state_v2;
  if (!PicoAppStateLoad(kStateTagV2, &state_v2, sizeof(state_v2)) ||
      state_v2.version != 2u) return false;
  sequencer.restoreState(state_v2.sequence, state_v2.steps);
  bass.restoreConfig(state_v2.bass);
  return true;
}

void AcidSaveState(const AcidSequencer &sequencer, const AcidBass &bass) {
  AcidPersistentStateV3 state = {};
  state.version = 3u;
  sequencer.copyState(&state.sequence, state.steps);
  state.bass = bass.config();
  AcidCVExportState(&state.cv);
  (void)PicoAppStateSave(kStateTagV3, &state, sizeof(state));
}
