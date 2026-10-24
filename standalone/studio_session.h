// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <vector>

#include "core/edit/audio_document.h"
#include "studio_engine.h"

namespace bc {
namespace studio {

struct SessionData {
    int schema = 1;
    std::string sourcePath;
    std::string sourceRelative;   // relativo al file sessione
    std::string sourceSha256;
    std::vector<EditRegion> regions;
    std::vector<Correction> corrections;
    FadeSettings fade;
    float masterGainDb = 0.0f;
    float reductionDb = -12.0f;
    double fadeMs = 15.0;
    double crossfadeMs = 5.0;
    std::string detector;
    AnalysisProvenance analysis;
    float aiThreshold = .064f;
    std::optional<DetectorSettings> detectorSettings;
    std::vector<Event> events;   // stato editoriale + modalita' per il piano
};

bool saveSession(const std::string& path, const SessionData& session,
                 std::string* error = nullptr);
bool loadSession(const std::string& path, SessionData* session,
                 std::string* error = nullptr);

} // namespace studio
} // namespace bc
