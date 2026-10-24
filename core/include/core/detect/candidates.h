// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  candidates.h — i candidati "larghi" che la rete deve vedere
//
//  Il rilevatore DSP propone solo cio' che supera le sue porte (flatness,
//  armonicita', centroide): un fiato tonale come quello a 17 s non passa mai,
//  quindi la rete non puo' neanche dire la sua. Qui si estraggono i candidati
//  con un criterio VOLUTAMENTE largo (armonicita' < 0.90, livello > -150 dB),
//  gli stessi con cui e' stato costruito il dataset di addestramento.
//
//  Il modello decide poi quali candidati sono fiati. La differenza col DSP e'
//  che qui la decisione non e' gia' stata presa dalle porte.
// ============================================================================
#pragma once

#include <vector>

namespace bc {

struct WideCandidate {
    int firstFrame = 0;
    int lastFrame = 0;
};

struct WideCandidateConfig {
    float harmonicityMax = 0.90f;
    float minLevelDb = -150.0f;
    int minDurationMs = 40;
    int maxDurationMs = 1200;
    // Buco massimo fra due frame che passano. 0 = contigui, come
    // l'estrattore che ha prodotto il dataset attuale.
    int maxGapFrames = 0;
};

std::vector<WideCandidate> findWideCandidates(const std::vector<float>& harmonicity,
                                              const std::vector<float>& levelDb,
                                              int hopSize,
                                              double sampleRate,
                                              const WideCandidateConfig& config = WideCandidateConfig());

} // namespace bc
