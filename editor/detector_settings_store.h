// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  detector_settings_store.h — impostazioni di rilevamento persistenti.
//
//  I parametri confermati vivono in %APPDATA%\BreathClickStudio. I preset
//  personali sono file .bcpreset nella stessa cartella. Non si usa il registro
//  di Windows e non si tocca il progetto finché l'utente non preme OK/Applica.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/detect/detection.h"

namespace bc {

enum class SensitivityPreset {
    Conservative = 0,
    Balanced = 1,
    High = 2
};

const char* sensitivityPresetName(SensitivityPreset preset);
void applySensitivityPreset(DetectorSettings& settings, SensitivityPreset preset);

std::string detectorSettingsDirectory();
bool loadDetectorSettings(DetectorSettings& settings, std::string* error = nullptr);
bool saveDetectorSettings(const DetectorSettings& settings, std::string* error = nullptr);

bool saveDetectorPreset(const std::string& path, const DetectorSettings& settings,
                        std::string* error = nullptr);
bool loadDetectorPreset(const std::string& path, DetectorSettings& settings,
                        std::string* error = nullptr);
std::vector<std::string> listDetectorPresets();

} // namespace bc
