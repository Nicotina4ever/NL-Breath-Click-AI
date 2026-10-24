// SPDX-License-Identifier: MPL-2.0
#include "core/dsp/breath_detector.h"

#include <algorithm>
#include <cmath>

namespace bc {

namespace {

constexpr float kSilenceDb = -160.0f;

float clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

float smoothstep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) return value >= edge1 ? 1.0f : 0.0f;
    const float x = clampf((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

} // namespace

BreathDetector::BreathDetector(const BreathDetectorConfig& config)
    : config_(config) {
    config_.floorWindowFrames = std::max(16, config_.floorWindowFrames);
    floorHistory_.assign(static_cast<size_t>(config_.floorWindowFrames), kSilenceDb);
}

BreathDetector::~BreathDetector() = default;

void BreathDetector::setSensitivity(float sensitivity) {
    config_.sensitivity = clampf(sensitivity, 0.0f, 1.0f);
}

void BreathDetector::setThresholdOffset(float offset) {
    thresholdOffset_ = clampf(offset, -1.0f, 1.0f);
}

void BreathDetector::reset() {
    std::fill(floorHistory_.begin(), floorHistory_.end(), kSilenceDb);
    floorCount_ = 0;
    floorWrite_ = 0;
    noiseFloorDb_ = -90.0f;
}

float BreathDetector::threshold() const {
    // 0.5 -> 0.55; sensibilità alta -> soglia più bassa.
    const float base = 0.62f - 0.30f * config_.sensitivity;
    return clampf(base - thresholdOffset_ * 0.25f, 0.10f, 0.95f);
}

BreathScore BreathDetector::process(const FrameFeatures& features, float voiceProbability) {
    BreathScore result;

    // ---------------------------------------------------- fondo di rumore
    // Tracker con discesa rapida e risalita lenta: il fondo si adatta subito
    // quando il rumore cala, ma non viene "trascinato" dalla voce.
    if (features.levelDb <= kSilenceDb + 1.0f) {
        // Frame digitale silenzioso: nessun aggiornamento del fondo.
    } else if (features.levelDb < noiseFloorDb_ || noiseFloorDb_ <= -89.0f) {
        noiseFloorDb_ = features.levelDb;
    } else {
        noiseFloorDb_ += 0.004f * (features.levelDb - noiseFloorDb_);
    }

    result.levelAboveFloorDb = features.levelDb - noiseFloorDb_;

    // Buffer circolare usato solo per il report del fondo (mediana recente).    floorHistory_[floorWrite_] = features.levelDb;
    floorWrite_ = (floorWrite_ + 1) % floorHistory_.size();
    if (floorCount_ < floorHistory_.size()) {
        ++floorCount_;
    }

    // ------------------------------------------------------ componenti
    // 1) Rumore a banda larga: spettro piatto.
    const float noiseLike = smoothstep(config_.minFlatness, config_.minFlatness * 3.5f,
                                       features.spectralFlatness);
    result.noiseLike = noiseLike;

    // 2) Assenza di voce. Il VAD e' il segnale preferito; la salienza armonica
    //    e' il ripiego quando il modello non e' disponibile.
    const float voicing = (voiceProbability >= 0.0f)
                              ? voiceProbability
                              : features.harmonicRatio;

    float unvoiced = 1.0f - smoothstep(0.25f, 0.60f, voicing);
    result.unvoiced = unvoiced;

    // 3) Energia sopra i 500 Hz: un respiro non e' un rimbombo.
    const float upperRatio = features.midBandRatio + features.highBandRatio;
    result.bandOk = smoothstep(0.18f, 0.55f, upperRatio);

    // 4) Il centroide sta in mezzo allo spettro, non in alto.
    //
    // E' il discriminante che separa davvero un respiro da una sibilante:
    // 4955 Hz contro 7334 Hz di mediana, distribuzioni che non si sovrappongono.
    // A differenza del confronto con i dintorni, funziona sul frame da solo,
    // quindi anche quando si parla.
    result.measuredCentroid = features.spectralCentroid;

    result.levelAboveFloorDb = features.levelDb - noiseFloorDb_;
    result.measuredFlatness = features.spectralFlatness;
    result.measuredHarmonicity = voicing;
    result.measuredLevelDb = features.levelDb;

    // ------------------------------------------------------ decisione
    // Tre condizioni indipendenti, ognuna sulle grandezze misurate.
    //
    // Il punteggio e' il margine della condizione PIU' DEBOLE: un "e" vale
    // quanto la sua condizione peggiore. Cosi' punteggio > 0 significa
    // esattamente "tutte e tre passano", e il valore dice anche a chi lo legge
    // quale delle tre e' il collo di bottiglia.

    // La sensibilita' allarga o stringe tutte e tre insieme.
    // 0.5 = valori di fabbrica, 1.0 = piu' permissivo, 0.0 = piu' severo.
    const float loosen =
        clampf((config_.sensitivity - 0.5f) * 2.0f - thresholdOffset_, -1.0f, 1.0f);

    const float flatnessGate = config_.flatnessGate * (1.0f - 0.30f * loosen);
    const float harmonicityGate = config_.harmonicityGate * (1.0f + 0.20f * loosen);
    const float levelGateDb = config_.levelGateDb - 6.0f * loosen;
    const float centroidGate = config_.centroidGate * (1.0f + 0.25f * loosen);

    const float flatnessMargin =
        clampf((features.spectralFlatness - flatnessGate) / 0.15f, 0.0f, 1.0f);
    const float voicingMargin =
        clampf((harmonicityGate - voicing) / 0.25f, 0.0f, 1.0f);
    const float levelMargin =
        clampf((features.levelDb - levelGateDb) / 15.0f, 0.0f, 1.0f);
    // Il centroide e' una PORTA, non un margine.
    //
    // Provato a metterlo nel minimo come gli altri: peggiora tutto. Il margine
    // normalizzato diventa il collo di bottiglia per quasi tutti i respiri veri
    // (centroide mediano 4955 Hz, quindi un margine di 0,5 su una soglia di
    // 0,25) e il rilevamento crolla da 340 a 13 respiri.
    //
    // Il dato dice altro: respiro 4955 Hz contro consonante 7334 Hz, con
    // distribuzioni che NON si sovrappongono. Non e' una questione di quanto,
    // e' dentro o fuori.
    if (features.spectralCentroid > centroidGate) {
        result.centroidMargin = 0.0f;
        result.score = 0.0f;
        result.detected = false;
        return result;
    }
    result.centroidMargin = 1.0f;

    const float weakest =
        std::min(flatnessMargin, std::min(voicingMargin, levelMargin));

    result.score = clampf(weakest, 0.0f, 1.0f);
    result.detected = result.score > 0.0f;
    return result;
}

} // namespace bc
