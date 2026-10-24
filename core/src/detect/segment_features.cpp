// SPDX-License-Identifier: MPL-2.0
#include "core/detect/segment_features.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bc {

namespace {

constexpr float kSilenceDb = -160.0f;
constexpr float kDigitalSilenceDb = -150.0f;

// Mediana di un vettore. Non usa nth_element sul vettore del chiamante:
// prende una copia perche' l'ordine dei frame non va toccato.
float mediana(std::vector<float> valori) {
    if (valori.empty()) return kSilenceDb;
    const size_t meta = valori.size() / 2;
    std::nth_element(valori.begin(), valori.begin() + static_cast<std::ptrdiff_t>(meta),
                     valori.end());
    return valori[meta];
}

} // namespace

float speechLevelDb75(const std::vector<float>& levelDb) {
    std::vector<float> ordinati;
    ordinati.reserve(levelDb.size());
    for (float valore : levelDb) {
        if (valore > kDigitalSilenceDb) ordinati.push_back(valore);
    }
    if (ordinati.size() < 32) return kSilenceDb;
    std::sort(ordinati.begin(), ordinati.end());
    return ordinati[static_cast<size_t>(static_cast<double>(ordinati.size()) * 0.75)];
}

std::vector<PauseRange> findPauses(const std::vector<float>& levelDb,
                                   int hopSize,
                                   double sampleRate,
                                   float thresholdDb,
                                   int minMs) {
    std::vector<PauseRange> pause;
    if (levelDb.empty() || hopSize <= 0 || sampleRate <= 0.0) return pause;

    const double hopSec = static_cast<double>(hopSize) / sampleRate;
    const double durataMinima = static_cast<double>(minMs) / 1000.0;

    auto chiudi = [&pause, hopSec, durataMinima](int inizio, int fine) {
        if (inizio < 0 || fine < inizio) return;
        const double durata = static_cast<double>(fine - inizio + 1) * hopSec;
        if (durata + 1e-9 < durataMinima) return;
        pause.push_back({static_cast<double>(inizio) * hopSec,
                         static_cast<double>(fine + 1) * hopSec});
    };

    int inizio = -1;
    for (size_t i = 0; i < levelDb.size(); ++i) {
        const bool silenzio = levelDb[i] < thresholdDb;
        if (silenzio) {
            if (inizio < 0) inizio = static_cast<int>(i);
        } else if (inizio >= 0) {
            chiudi(inizio, static_cast<int>(i) - 1);
            inizio = -1;
        }
    }
    if (inizio >= 0) chiudi(inizio, static_cast<int>(levelDb.size()) - 1);
    return pause;
}

BreathSegmentFeatures computeSegmentFeatures(const FrameTracks& tracks,
                                             int firstFrame,
                                             int lastFrame,
                                             int64_t startSample,
                                             int64_t endSample) {
    BreathSegmentFeatures f;

    // Tutti i vettori devono essere allineati; se uno e' piu' corto ci si
    // ferma al piu' corto invece di leggere fuori.
    size_t frameCount = tracks.levelDb.size();
    frameCount = std::min(frameCount, tracks.harmonicity.size());
    frameCount = std::min(frameCount, tracks.flatness.size());
    frameCount = std::min(frameCount, tracks.centroid.size());
    frameCount = std::min(frameCount, tracks.zcr.size());
    frameCount = std::min(frameCount, tracks.lowBand.size());
    frameCount = std::min(frameCount, tracks.midBand.size());
    frameCount = std::min(frameCount, tracks.highBand.size());

    if (frameCount == 0 || tracks.sampleRate <= 0.0 || tracks.hopSize <= 0) return f;
    if (firstFrame < 0) firstFrame = 0;
    if (lastFrame < firstFrame) return f;
    if (lastFrame >= static_cast<int>(frameCount)) lastFrame = static_cast<int>(frameCount) - 1;

    const double hopSec = static_cast<double>(tracks.hopSize) / tracks.sampleRate;

    f.durataMs = static_cast<float>(
        static_cast<double>(endSample - startSample) * 1000.0 / tracks.sampleRate);
    if (f.durataMs <= 0.0f) {
        f.durataMs = static_cast<float>(
            static_cast<double>(lastFrame - firstFrame + 1) * hopSec * 1000.0);
    }

    double sommaFlat = 0.0;
    double sommaHarm = 0.0;
    double sommaDb = 0.0;
    double sommaCent = 0.0;
    double sommaZcr = 0.0;
    double sommaBm = 0.0;
    double sommaBa = 0.0;
    float centMin = 1e9f;
    float centMax = -1e9f;
    size_t conta = 0;

    for (int k = firstFrame; k <= lastFrame; ++k) {
        sommaFlat += tracks.flatness[static_cast<size_t>(k)];
        sommaHarm += tracks.harmonicity[static_cast<size_t>(k)];
        sommaDb += tracks.levelDb[static_cast<size_t>(k)];
        sommaCent += tracks.centroid[static_cast<size_t>(k)];
        sommaZcr += tracks.zcr[static_cast<size_t>(k)];
        sommaBm += tracks.midBand[static_cast<size_t>(k)];
        sommaBa += tracks.highBand[static_cast<size_t>(k)];
        centMin = std::min(centMin, tracks.centroid[static_cast<size_t>(k)]);
        centMax = std::max(centMax, tracks.centroid[static_cast<size_t>(k)]);
        ++conta;
    }
    if (conta == 0) return f;

    const float n = static_cast<float>(conta);
    f.flatMedia = static_cast<float>(sommaFlat) / n;
    f.harmMedia = static_cast<float>(sommaHarm) / n;
    f.dbMedia = static_cast<float>(sommaDb) / n;
    f.centMedia = static_cast<float>(sommaCent) / n;
    f.centMin = centMin;
    f.centMax = centMax;
    f.zcrMedia = static_cast<float>(sommaZcr) / n;
    f.bmediaMedia = static_cast<float>(sommaBm) / n;
    f.baltaMedia = static_cast<float>(sommaBa) / n;

    // Dintorni: mediana dei livelli a +-250 ms, escluso il segmento. E' la
    // definizione dell'addestramento, non il P75 globale del file.
    const int raggio = std::max(1, static_cast<int>(0.25 / hopSec));
    const int daFrame = std::max(0, firstFrame - raggio);
    const int aFrame = std::min(static_cast<int>(frameCount) - 1, lastFrame + raggio);
    std::vector<float> vicini;
    vicini.reserve(static_cast<size_t>(aFrame - daFrame + 1));
    for (int k = daFrame; k <= aFrame; ++k) {
        if (k >= firstFrame && k <= lastFrame) continue;
        const float livello = tracks.levelDb[static_cast<size_t>(k)];
        if (livello > kDigitalSilenceDb) vicini.push_back(livello);
    }
    f.viciniDb = mediana(std::move(vicini));
    f.sottoViciniDb = f.viciniDb - f.dbMedia;
    f.sopraFondoDb = f.dbMedia - tracks.noiseFloorDb;

    // Una pausa contiene il segmento se lo copre con 150 ms di tolleranza,
    // la stessa usata per etichettare gli eventi in addestramento.
    const double t0 = static_cast<double>(firstFrame) * hopSec;
    const double t1 = static_cast<double>(lastFrame) * hopSec;
    f.inPausa = 0.0f;
    for (const PauseRange& p : tracks.pauses) {
        if (t0 >= p.startSeconds - 0.15 && t1 <= p.endSeconds + 0.15) {
            f.inPausa = 1.0f;
            break;
        }
    }

    return f;
}

} // namespace bc
