#ifndef PICOPRO_ACID_CV_H_
#define PICOPRO_ACID_CV_H_

#include <Arduino.h>

#include "AcidBass.h"
#include "CVPersistence.h"
#include "ClickEncoder.h"

constexpr uint8_t ACID_CV_PARAMETER_COUNT = 10u;

void AcidCVBegin(const AcidBassConfig &config);
void AcidCVServiceModulation(AcidBass &bass);
uint8_t AcidCVServiceOverlay(int16_t movement, bool button_down,
                             ClickEncoder::Button button);
bool AcidCVHandleEntry(ClickEncoder::Button button, uint8_t index);
void AcidCVDrawIndicator(uint8_t index);
int16_t AcidCVDisplayValue(uint8_t index, int16_t base);
bool AcidCVShouldRefresh(uint8_t index, uint32_t now_ms);
void AcidCVExportState(PicoCVPersistentState *state);
void AcidCVImportState(const PicoCVPersistentState *state);

#endif
