// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  frame_features.h — Estrazione delle feature per frame
//
//  E' la base condivisa da tutti i rilevatori: le stesse feature alimentano
//  il rilevatore di respiri, quello di click, l'analisi offline e il trainer.
//
//  Vincolo importante: analyze() non alloca memoria. Tutti i buffer sono
//  preallocati nel costruttore, cosi' la stessa classe e' utilizzabile sia sul
//  thread di analisi del plugin sia offline.
// ============================================================================
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "kiss_fftr.h"

namespace bc {

struct FrameFeatures {
    // --- Dominio del tempo -------------------------------------------------
    float rms = 0.0f;             // valore efficace
    float levelDb = -160.0f;      // 20*log10(rms), limite inferiore -160 dBFS
    float peak = 0.0f;            // massimo valore assoluto
    float crest = 0.0f;           // peak / rms  (alto = transiente)
    float zcr = 0.0f;             // attraversamenti dello zero per campione
    float highFreqRatio = 0.0f;   // energia della derivata / energia totale

    // --- Dominio spettrale (magnitudini) -----------------------------------
    float spectralCentroid = 0.0f;  // Hz
    float spectralSpread = 0.0f;    // Hz
    float spectralFlatness = 0.0f;  // 0 = tono puro, 1 = rumore bianco
    float spectralRolloff = 0.0f;   // Hz, sotto cui sta l'85% dell'energia
    float spectralCrest = 0.0f;     // massimo / media delle magnitudini
    float spectralFlux = 0.0f;      // flusso positivo relativo vs frame precedente

    // --- Bande -------------------------------------------------------------
    // Rapporti (sommiano a 1 sull'intervallo 80 Hz - 10 kHz).
    float lowBandRatio = 0.0f;   // 80 - 500 Hz
    float midBandRatio = 0.0f;   // 500 - 2000 Hz
    float highBandRatio = 0.0f;  // 2000 - 10000 Hz
    // Energie assolute (media dei quadrati delle magnitudini nella banda),
    // necessarie per il rilevatore di click basato su salto di energia in alta
    // frequenza. Indipendenti dal livello complessivo del brano.
    float highBandEnergy = 0.0f;
    float highBandLevelDb = -160.0f;

    // --- Periodicità -------------------------------------------------------
    float harmonicRatio = 0.0f;     // salienza armonica 0..1 (autocorrelazione)
    float pitchHz = 0.0f;           // frequenza fondamentale stimata, 0 se assente
    float envelopeSlopeDb = 0.0f;   // variazione di livello vs frame precedente
};

class FrameAnalyzer {
public:
    struct Config {
        int fftSize = 1024;
        float sampleRate = 48000.0f;
        float pitchMinHz = 70.0f;
        float pitchMaxHz = 1000.0f;
        int decimation = 4;         // decimazione per la stima di pitch
    };

    explicit FrameAnalyzer(const Config& config);
    ~FrameAnalyzer();

    FrameAnalyzer(const FrameAnalyzer&) = delete;
    FrameAnalyzer& operator=(const FrameAnalyzer&) = delete;

    const Config& config() const { return config_; }
    int fftSize() const { return config_.fftSize; }
    float hopDurationMs(int hopSize) const {
        return 1000.0f * static_cast<float>(hopSize) / config_.sampleRate;
    }

    // Analizza un frame. Se frameSize > fftSize vengono usati gli ultimi
    // fftSize campioni; se minore, il frame viene allineato a destra e
    // riempito di zeri davanti. Non alloca.
    //
    // Ritorna per VALORE di proposito: restituire un riferimento a un membro
    // interno significherebbe che il risultato di una chiamata precedente
    // cambia dopo la chiamata successiva, un errore facile da commettere.
    // FrameFeatures e' piccolo (poche decine di byte) e la copia e' gratuita
    // rispetto al costo della FFT.
    FrameFeatures analyze(const float* samples, size_t frameSize);

    // Azzera la memoria del frame precedente.
    void reset();

    // Magnitudini dell'ultimo spettro analizzato (fftSize/2+1 valori).
    const std::vector<float>& lastMagnitudes() const { return magnitude_; }

private:
    void buildWindow();
    float estimateHarmonicity();

    Config config_;
    int numBins_ = 0;
    int lagMin_ = 2;
    int lagMax_ = 3;
    int decimatedCount_ = 0;
    float decimatedRate_ = 12000.0f;

    std::unique_ptr<kiss_fftr_state, void (*)(void*)> fftCfg_;
    std::vector<float> window_;
    std::vector<float> fftInput_;
    std::vector<kiss_fft_cpx> fftOutput_;
    std::vector<float> magnitude_;
    std::vector<float> previousMagnitude_;
    std::vector<float> centered_;
    std::vector<float> decimated_;
    std::vector<float> bandEdges_;   // indici dei bordi 80/500/2000/10000 Hz

    FrameFeatures features_;
    float previousRms_ = 0.0f;
    bool hasPrevious_ = false;
};

} // namespace bc
