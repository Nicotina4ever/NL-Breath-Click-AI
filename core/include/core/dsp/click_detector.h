// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  click_detector.h — Rilevamento click/pop (puramente DSP)
//
//  PERCHE' NON C'E' UN MODELLO ONNX
//  Il file "click-detector.onnx" che il progetto si portava dietro era in
//  realta' un Vision Transformer per immagini (343 MB, input "pixel_values"
//  4D): non era un modello audio e non poteva funzionare. Il codice lo
//  rilevava e ripiegava su un finto punteggio binario (0.9 / 0.2), motivo per
//  cui il rilevamento click era inaffidabile.
//
//  COME FUNZIONA — errore di predizione
//  E' l'approccio classico del de-clicking audio. Un click e' un campione (o
//  pochi campioni) che il resto del segnale NON riesce a predire. Si calcola
//  quindi l'errore di un predittore del secondo ordine:
//
//        d[n] = x[n] - (x[n-1] + x[n+1]) / 2
//
//  Per un segnale a banda limitata — la voce — d[n] e' piccolissimo. Per un
//  impulso d[n] e' grande quanto l'impulso stesso. Il rapporto fra il picco di
//  |d| nella sotto-finestra e la MEDIANA di |d| sulla storia recente e' quindi
//  una misura di impulsivita' robusta e indipendente dal livello: funziona
//  identica in una pausa e in mezzo al canto.
//
//  Al rapporto si aggiunge una verifica di LARGHEZZA: un click interessa 1-3
//  campioni, mentre un attacco di nota o una plosiva hanno una salita larga.
//  Senza questo controllo gli attacchi verrebbero scambiati per click.
//
//  Nessuna allocazione in processBlock().
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>
#include "core/detect/detection.h"

namespace bc {

struct ClickDetectorConfig {
    float sampleRate = 48000.0f;

    // Sotto-finestra di analisi: 64 campioni a 48 kHz = 1.33 ms.
    int subWindowSamples = 64;

    // Storia per la stima robusta della mediana di |d|.
    // 200 sotto-finestre ~ 0.27 s a 48 kHz.
    int historyFrames = 200;

    // Rapporto picco/mediana a cui il click e' certo.
    // Un click reale supera 1000; il rumore di un respiro arriva a 20-100 e la
    // voce resta sotto 12. La soglia sta quindi in una zona di separazione
    // molto ampia, non a ridosso di nessuno dei due fenomeni.
    float ratioLow = 25.0f;
    float ratioHigh = 120.0f;

    // Frazione massima di campioni sopra meta' del picco perche' l'evento sia
    // considerato "stretto" (un click).
    float narrowFraction = 0.15f;

    // Livello minimo del picco perche' l'evento sia considerato reale.
    float minPeakLevel = 1.0e-4f;

    // Sensibilita' utente 0..1 (0.5 = neutra).
    float sensitivity = 0.5f;
};

struct ClickScore {
    float score = 0.0f;        // 0..1
    float ratio = 0.0f;        // picco |d| / mediana |d|
    float peak = 0.0f;         // picco di |d| nella sotto-finestra
    float median = 0.0f;       // mediana corrente di |d|
    float narrowness = 0.0f;   // 1 = molto stretto (click), 0 = largo
    int peakIndex = -1;        // indice del picco nel blocco analizzato
    bool detected = false;
};

class ClickDetector {
public:
    explicit ClickDetector(const ClickDetectorConfig& config);
    ~ClickDetector();

    ClickDetector(const ClickDetector&) = delete;
    ClickDetector& operator=(const ClickDetector&) = delete;

    const ClickDetectorConfig& config() const { return config_; }
    int subWindowSamples() const { return config_.subWindowSamples; }

    void setSensitivity(float sensitivity);
    void reset();

    // Elabora un blocco (idealmente di subWindowSamples campioni) e ritorna il
    // punteggio complessivo. Non alloca.
    ClickScore processBlock(const float* samples, size_t count);

    float medianAbsError() const { return medianAbsError_; }

private:
    float robustMedian();
    void pushHistory(float value);

    ClickDetectorConfig config_;
    std::vector<float> history_;
    std::vector<float> sortScratch_;
    size_t historyCount_ = 0;
    size_t historyWrite_ = 0;

    // Stato del predittore (ritardo di un campione).
    double delayA_ = 0.0;
    double delayB_ = 0.0;
    int delayCount_ = 0;

    float medianAbsError_ = 0.0f;
};

// Context-aware offline candidates; classifier names are DSP hypotheses, not labels.
std::vector<Detection> detectMouthClicks(const float* samples, size_t count, double sampleRate,
                                       const DetectorSettings& settings, const std::function<bool()>& cancelled = {});

} // namespace bc
