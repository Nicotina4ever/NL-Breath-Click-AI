// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  resampler.h — Ricampionamento lineare con filtro anti-aliasing
//
//  Serve al gate vocale: il modello Silero VAD lavora a 16 kHz, mentre le
//  registrazioni sono tipicamente a 44.1 / 48 / 96 kHz. Il ricampionatore
//  filtra in passa-basso alla frequenza di Nyquist di uscita prima di
//  interpolare, evitando che l'aliasing peggiori la stima di voce.
//
//  Non alloca durante il processamento dopo configure().
// ============================================================================
#pragma once

#include <cstddef>
#include <vector>

namespace bc {

class LinearResampler {
public:
    LinearResampler() = default;

    // Configura il rapporto di conversione. Ritorna false se i rate non sono
    // validi.
    bool configure(double inputRate, double outputRate);

    void reset();

    bool isConfigured() const { return configured_; }
    double inputRate() const { return inputRate_; }
    double outputRate() const { return outputRate_; }
    double ratio() const { return ratio_; }

    // Ricampiona e ACCoda il risultato in output. Il vettore di output non
    // viene svuotato: il chiamante puo' concatenare piu' blocchi.
    void process(const float* input, size_t count, std::vector<float>& output);

    // Quanti campioni produce process() per count campioni di ingresso.
    size_t outputCountFor(size_t count) const;

private:
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

        double process(double x) {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }

        void reset() { x1 = x2 = y1 = y2 = 0.0; }
    };

    void designLowpass(double cutoffHz);

    bool configured_ = false;
    double inputRate_ = 0.0;
    double outputRate_ = 0.0;
    double ratio_ = 1.0;         // outputRate / inputRate
    double phase_ = 0.0;
    float previousInput_ = 0.0f;
    bool hasPrevious_ = false;

    std::vector<Biquad> filters_;   // 2 stadi in cascata
    std::vector<float> filtered_;
    bool filtering_ = false;
};

} // namespace bc
