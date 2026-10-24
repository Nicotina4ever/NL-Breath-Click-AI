// SPDX-License-Identifier: MPL-2.0
#include "core/detect/candidates.h"

#include <algorithm>

namespace bc {

std::vector<WideCandidate> findWideCandidates(const std::vector<float>& harmonicity,
                                              const std::vector<float>& levelDb,
                                              int hopSize,
                                              double sampleRate,
                                              const WideCandidateConfig& config) {
    std::vector<WideCandidate> candidati;
    if (hopSize <= 0 || sampleRate <= 0.0) return candidati;

    const size_t frameCount = std::min(harmonicity.size(), levelDb.size());
    if (frameCount == 0) return candidati;

    const double hopSec = static_cast<double>(hopSize) / sampleRate;

    int inizio = -1;
    int ultimo = -1;
    int buco = 0;

    auto chiudi = [&]() {
        if (inizio < 0) return;
        const double durata = static_cast<double>(ultimo - inizio + 1) * hopSec * 1000.0;
        if (durata + 1e-9 >= static_cast<double>(config.minDurationMs) &&
            durata <= static_cast<double>(config.maxDurationMs) + 1e-9) {
            candidati.push_back({inizio, ultimo});
        }
        inizio = -1;
        ultimo = -1;
        buco = 0;
    };

    for (size_t i = 0; i < frameCount; ++i) {
        const bool passa = harmonicity[i] < config.harmonicityMax &&
                           levelDb[i] > config.minLevelDb;
        if (passa) {
            if (inizio < 0) inizio = static_cast<int>(i);
            ultimo = static_cast<int>(i);
            buco = 0;
        } else if (inizio >= 0) {
            ++buco;
            if (buco > config.maxGapFrames) chiudi();
        }
    }
    chiudi();
    return candidati;
}

} // namespace bc
