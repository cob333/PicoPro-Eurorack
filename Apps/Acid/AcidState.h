#ifndef PICOPRO_ACID_STATE_H_
#define PICOPRO_ACID_STATE_H_

#include "AcidBass.h"
#include "AcidSequencer.h"

bool AcidLoadState(AcidSequencer &sequencer, AcidBass &bass);
void AcidSaveState(const AcidSequencer &sequencer, const AcidBass &bass);

#endif
