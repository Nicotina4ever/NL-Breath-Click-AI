// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  personal_profile.h — primo livello reale di apprendimento dalle correzioni.
//
//  NON allena e NON modifica i pesi ONNX di Respiro-en. Raccoglie esempi
//  esplicitamente confermati dall'utente e calibra un piccolo profilo
//  personale (score del modello + feature DSP) che puo' essere attivato o
//  disattivato. Senza dati sufficienti o senza una valutazione indipendente
//  il profilo viene rifiutato e si continua col rilevatore generale.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/edit/audio_document.h"

namespace bc {

enum class LearningLabel {
    Unverified = 0,
    BreathConfirmed,
    FalsePositive,
    MissedBreath,
    EditorialExclusion
};

const char* learningLabelName(LearningLabel label);
LearningLabel learningLabelFromName(const std::string& name);

struct LearningFeatures {
    float modelScore = 0.0f;   // score del rilevatore (0..1)
    float durationMs = 0.0f;
    float rmsDb = -160.0f;     // feature DSP
    float zeroCrossRate = 0.0f;
    float meanAbs = 0.0f;      // ampiezza media, feature DSP
};

struct LearningExample {
    LearningLabel label = LearningLabel::Unverified;
    std::string sourcePath;
    std::string recordingId; // SHA-256 decoded PCM + format; copies share a split
    int64_t startFrame = 0;
    int64_t endFrame = 0;
    bool baselineAccepted = true; // proposed by the general detector before review
    LearningFeatures features;
};

struct PersonalProfile {
    int schema = 2;
    bool valid = false;
    float bias = 0.0f;
    float weightScore = 0.0f;
    float weightDuration = 0.0f;
    float weightRms = 0.0f;
    float weightZcr = 0.0f;
    float weightMean = 0.0f;
    float threshold = 0.5f;
    int positives = 0;
    int negatives = 0;
    int validationExamples = 0;
    double validationBalancedAccuracy = 0.0;
    double baselineBalancedAccuracy = 0.0;
    int validationMisses = 0;
    int validationFalsePositives = 0;
    int baselineMisses = 0;
    int baselineFalsePositives = 0;
    std::string validationSource;
    std::vector<std::string> sources;
    std::string createdAt;

    float probability(const LearningFeatures& features) const;
    bool accepts(const LearningFeatures& features) const {
        return valid && probability(features) >= threshold;
    }
};

LearningFeatures extractLearningFeatures(const AudioBuffer& audio,
                                         int64_t startFrame, int64_t endFrame,
                                         float modelScore);

// Costruisce gli esempi solo dalle etichette esplicite. Esclusioni editoriali
// e eventi non verificati non entrano nel training.
std::vector<LearningExample> collectLearningExamples(const AudioDocument& document);

// Richiede esempi positivi e negativi da almeno due file distinti e una
// validazione su file separati. In caso contrario restituisce false e spiega
// il motivo; non produce un profilo "finto".
bool trainPersonalProfile(const std::vector<LearningExample>& examples,
                          PersonalProfile& profile, std::string* error);

bool savePersonalProfile(const std::string& path, const PersonalProfile& profile,
                         std::string* error = nullptr);
bool loadPersonalProfile(const std::string& path, PersonalProfile& profile,
                         std::string* error = nullptr);

} // namespace bc
