// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  segment_features.h — le 14 grandezze di un segmento, in un posto solo
//
//  PERCHE' ESISTE QUESTO FILE
//  --------------------------
//  Prima le stesse 14 grandezze erano calcolate DUE VOLTE: in Python
//  (addestra_estrai_dataset.py) per addestrare, e in C++ (offline_analyzer.cpp)
//  per usare la rete. Le due versioni non coincidono:
//
//      feature         Python                    C++
//      vicini_db       mediana locale +-250 ms   P75 globale del file
//      sopra_fondo_db  livello - fondo rumore    livello - livello parlato
//      in_pausa        ffmpeg silencedetect       db < P75 - 12
//
//  Due feature su tre avevano segno e scala diversi: la rete riceveva in uso
//  numeri che non aveva mai visto in addestramento. Qui la definizione e' UNA:
//  la usano l'analizzatore, il dump dei candidati e (tramite il dump) il
//  dataset di addestramento.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "core/detect/breath_model_eval.h"

namespace bc {

// Un intervallo di pausa, in secondi dall'inizio del file.
struct PauseRange {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
};

// Le tracce per frame prodotte dalla prima passata dell'analizzatore.
// I vettori sono allineati: stesso indice = stesso frame.
struct FrameTracks {
    std::vector<float> harmonicity;
    std::vector<float> flatness;
    std::vector<float> centroid;
    std::vector<float> zcr;
    std::vector<float> levelDb;
    std::vector<float> lowBand;
    std::vector<float> midBand;
    std::vector<float> highBand;
    int hopSize = 256;
    double sampleRate = 48000.0;
    float noiseFloorDb = -160.0f;
    // 75esimo percentile dei livelli: il riferimento che il DSP chiama
    // "livello del parlato". Non e' una feature del modello, serve al gate.
    float speechLevelDb = -160.0f;
    std::vector<PauseRange> pauses;
};

// P75 dei livelli sopra il silenzio digitale. Sotto 32 frame restituisce
// -160 dB: con pochi punti un percentile non significa niente.
float speechLevelDb75(const std::vector<float>& levelDb);

// Pause: frame sotto thresholdDb per almeno minMs. La soglia e' ASSOLUTA,
// come il silencedetect di ffmpeg usato finora (-42 dB, 0.12 s): un respiro
// non deve cancellare la pausa che lo contiene.
std::vector<PauseRange> findPauses(const std::vector<float>& levelDb,
                                   int hopSize,
                                   double sampleRate,
                                   float thresholdDb = -42.0f,
                                   int minMs = 120);

// Le 14 grandezze di un segmento [firstFrame, lastFrame].
//
// startSample/endSample servono solo per durataMs: per un candidato largo
// coincidono con firstFrame*hop e (lastFrame+1)*hop, per un rilevamento DSP
// possono non essere allineati ai frame. Le altre grandezze usano i frame.
BreathSegmentFeatures computeSegmentFeatures(const FrameTracks& tracks,
                                             int firstFrame,
                                             int lastFrame,
                                             int64_t startSample,
                                             int64_t endSample);

} // namespace bc
