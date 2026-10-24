// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  silence_gate.h - Rilevamento del silenzio con isteresi
//
//  Replica il comportamento del "Dynamic split" di REAPER, che e' il
//  riferimento che l'utente usa gia' e di cui si fida:
//
//      Threshold        -48 dB     sotto questa soglia si entra in silenzio
//      Isteresi           6 dB     si esce solo sopra soglia + isteresi
//      Lunghezza min      20 ms    il suono piu' corto di cosi' e' un click
//      Lunghezza min     200 ms    il silenzio piu' corto di cosi' non conta
//      Fade in/out        30 ms    suggerimento per i tagli
//
//  PERCHE' L'ISTERESI E' ESSENZIALE
//  ---------------------------------
//  Con una soglia secca, un respiro a -43 dB sta SOPRA la soglia di -48 e
//  quindi SPEZZA IN DUE la pausa che lo contiene. Il respiro cancella la
//  propria pausa: e' il motivo per cui il rilevamento del contesto falliva da
//  giorni, e non era un problema di taratura.
//
//  Con l'isteresi, una volta entrati in silenzio ci si resta finche' il livello
//  non supera soglia + isteresi (-42 dB). Un respiro a -43 dB non fa uscire, e
//  la pausa resta intera.
//
//  Le due durate minime servono a non scambiare per silenzio una micro-pausa
//  fra due sillabe, e per non scambiare per suono un click isolato.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace bc {

struct SilenceGateConfig {
    float thresholdDb = -48.0f;   // sotto: si entra in silenzio
    float hysteresisDb = 6.0f;    // sopra soglia + questa: si esce
    int minSilenceMs = 200;       // silenzi piu' corti vengono scartati
    int minSoundMs = 20;          // suoni piu' corti vengono assorbiti
    int fadeMs = 30;              // rampa suggerita per i tagli
};

// Un intervallo di silenzio, in campioni.
struct SilenceRange {
    int64_t startSample = 0;
    int64_t endSample = 0;

    int64_t lengthSamples() const { return endSample - startSample; }
    double lengthMs(double sampleRate) const {
        return sampleRate > 0.0
                   ? 1000.0 * static_cast<double>(lengthSamples()) / sampleRate
                   : 0.0;
    }
    double startSeconds(double sampleRate) const {
        return sampleRate > 0.0 ? static_cast<double>(startSample) / sampleRate : 0.0;
    }
    double endSeconds(double sampleRate) const {
        return sampleRate > 0.0 ? static_cast<double>(endSample) / sampleRate : 0.0;
    }
};

// Lavora su un inviluppo di livello gia' calcolato (un valore per frame), che
// e' quello che l'analizzatore produce comunque. Cosi' non si ri-analizza
// l'audio una seconda volta.
std::vector<SilenceRange> detectSilence(const std::vector<float>& frameLevelDb,
                                        int64_t hopSize,
                                        double sampleRate,
                                        const SilenceGateConfig& config);

} // namespace bc
