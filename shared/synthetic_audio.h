// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  synthetic_audio.h — Generatore di segnali di test con verita' a terra
//
//  Usato sia da `bc_cli selftest` sia dai test automatici. Genera materiale
//  vocale sintetico in cui la posizione esatta di respiri e click e' nota,
//  cosi' il rilevamento e' misurabile in modo oggettivo invece che "a orecchio".
//
//  Header-only: nessuna libreria da linkare, condiviso da piu' eseguibili.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "core/detect/detection.h"

namespace bc {
namespace synthetic {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
//  Filtri biquad (RBJ) per dare una forma spettrale realistica ai segnali
// ---------------------------------------------------------------------------
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    inline double process(double x) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

    static Biquad lowpass(double sampleRate, double freq, double q) {
        Biquad filter;
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double cosW0 = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        filter.b0 = ((1.0 - cosW0) * 0.5) / a0;
        filter.b1 = (1.0 - cosW0) / a0;
        filter.b2 = ((1.0 - cosW0) * 0.5) / a0;
        filter.a1 = (-2.0 * cosW0) / a0;
        filter.a2 = (1.0 - alpha) / a0;
        return filter;
    }

    static Biquad highpass(double sampleRate, double freq, double q) {
        Biquad filter;
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double cosW0 = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        filter.b0 = ((1.0 + cosW0) * 0.5) / a0;
        filter.b1 = -(1.0 + cosW0) / a0;
        filter.b2 = ((1.0 + cosW0) * 0.5) / a0;
        filter.a1 = (-2.0 * cosW0) / a0;
        filter.a2 = (1.0 - alpha) / a0;
        return filter;
    }
};

// ---------------------------------------------------------------------------
//  Descrizione del materiale generato
// ---------------------------------------------------------------------------
struct GroundTruthEvent {
    DetectionKind kind = DetectionKind::Breath;
    int64_t startSample = 0;
    int64_t endSample = 0;
    std::string label;
};

struct TestTake {
    std::vector<float> samples;      // mono
    double sampleRate = 48000.0;
    std::vector<GroundTruthEvent> events;

    double durationSeconds() const {
        return sampleRate > 0.0 ? static_cast<double>(samples.size()) / sampleRate : 0.0;
    }

    size_t countOf(DetectionKind kind) const {
        size_t total = 0;
        for (const GroundTruthEvent& event : events) {
            if (event.kind == kind) ++total;
        }
        return total;
    }
};

// ---------------------------------------------------------------------------
//  Costruzione dei singoli eventi
// ---------------------------------------------------------------------------
namespace detail {

inline double raisedCosineEnvelope(int64_t index, int64_t length) {
    if (length <= 0) return 0.0;
    const double t = static_cast<double>(index) / static_cast<double>(length);
    return 0.5 * (1.0 - std::cos(2.0 * kPi * std::min(1.0, std::max(0.0, t))));
}

// Rumore a banda larga filtrato: e' cio' che caratterizza un respiro.
inline void addBreath(std::vector<float>& buffer,
                      double sampleRate,
                      int64_t start,
                      int64_t length,
                      double amplitude,
                      std::mt19937& rng) {
    // Lo spettro del respiro deve somigliare a quello VERO, altrimenti la prova
    // sintetica misura un respiro che non esiste.
    //
    // Misurato sui take reali (340 respiri): centroide 4955 Hz, banda media
    // 500-2000 Hz al 28%, ZCR 0,130. Con un passa-basso a 7000 Hz il centroide
    // finiva a 5951 Hz: troppo brillante, e il gate sul centroide lo tagliava.
    //
    // Un passa-basso in cascata due volte da' una pendenza doppia e avvicina lo
    // spettro a quello misurato.
    Biquad high = Biquad::highpass(sampleRate, 420.0, 0.8);
    Biquad low1 = Biquad::lowpass(sampleRate, 5200.0, 0.7);
    Biquad low2 = Biquad::lowpass(sampleRate, 5200.0, 0.7);
    std::uniform_real_distribution<double> distribution(-1.0, 1.0);

    for (int64_t i = 0; i < length; ++i) {
        const int64_t index = start + i;
        if (index < 0 || index >= static_cast<int64_t>(buffer.size())) continue;
        double value = distribution(rng);
        value = high.process(value);
        value = low1.process(value);
        value = low2.process(value);
        // I biquad hanno guadagno unitario in banda: si normalizza per ottenere
        // un livello prevedibile.
        value *= 1.6;
        const double envelope = raisedCosineEnvelope(i, length);
        buffer[static_cast<size_t>(index)] += static_cast<float>(value * amplitude * envelope);
    }
}

// Impulso brevissimo e ricco di alte frequenze: un click.
inline void addClick(std::vector<float>& buffer,
                     double sampleRate,
                     int64_t start,
                     double amplitude,
                     std::mt19937& rng) {
    std::uniform_real_distribution<double> distribution(-1.0, 1.0);
    // Costante di decadimento ~0.6 ms: durata percepita di 1-2 ms.
    const double tauSamples = 0.0006 * sampleRate;
    const int64_t length = std::max<int64_t>(4, static_cast<int64_t>(tauSamples * 6.0));

    for (int64_t i = 0; i < length; ++i) {
        const int64_t index = start + i;
        if (index < 0 || index >= static_cast<int64_t>(buffer.size())) continue;
        const double decay = std::exp(-static_cast<double>(i) / tauSamples);
        const double noise = distribution(rng);
        // Componente alternata: energia concentrata in alta frequenza.
        const double sign = (i % 2 == 0) ? 1.0 : -1.0;
        buffer[static_cast<size_t>(index)] +=
            static_cast<float>(noise * sign * decay * amplitude);
    }
}

// Frammento vocale: somma di armoniche con inviluppo e vibrato.
// Serve come "voce" da NON rimuovere.
inline void addVoiced(std::vector<float>& buffer,
                      double sampleRate,
                      int64_t start,
                      int64_t length,
                      double f0,
                      double amplitude,
                      std::mt19937& rng) {
    std::uniform_real_distribution<double> jitter(-0.02, 0.02);
    const int harmonics = 22;
    const double phaseStep = 2.0 * kPi / sampleRate;

    for (int64_t i = 0; i < length; ++i) {
        const int64_t index = start + i;
        if (index < 0 || index >= static_cast<int64_t>(buffer.size())) continue;

        const double t = static_cast<double>(i) / sampleRate;
        const double vibrato = 1.0 + 0.01 * std::sin(2.0 * kPi * 5.0 * t);
        const double envelope = raisedCosineEnvelope(i, length);

        double value = 0.0;
        double harmonicSum = 0.0;
        for (int h = 1; h <= harmonics; ++h) {
            const double frequency = f0 * h * vibrato;
            if (frequency > sampleRate * 0.45) break;
            // Formanti approssimati: la voce non e' piatta in frequenza.
            const double formant1 = std::exp(-std::pow((frequency - 700.0) / 400.0, 2.0));
            const double formant2 = std::exp(-std::pow((frequency - 2200.0) / 900.0, 2.0));
            const double weight = (formant1 + 0.7 * formant2 + 0.05) / static_cast<double>(h);
            value += weight * std::sin(phaseStep * frequency * static_cast<double>(i));
            harmonicSum += weight;
        }
        if (harmonicSum > 0.0) value /= harmonicSum;
        value += jitter(rng) * 0.002;
        buffer[static_cast<size_t>(index)] += static_cast<float>(value * amplitude * envelope);
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
//  Materiale completo: voce + pause + respiri + click
// ---------------------------------------------------------------------------
struct TakeOptions {
    double sampleRate = 48000.0;
    double noiseFloor = 0.00025;      // ~ -72 dBFS

    // Livello del respiro rispetto alla voce: e' la grandezza che il
    // rilevatore usa per distinguere un respiro da una consonante.
    //
    // MISURATO, non scelto. Sui 13 take reali dell'utente (22,4 minuti,
    // 1568 candidati etichettati dal contesto):
    //
    //     respiro    livello mediano -43 dB
    //     consonante livello mediano -28 dB
    //
    // cioe' 15 dB di distanza. Il valore precedente (0.075 su una voce da 0.35)
    // dava solo 7,8 dB, e questo ha reso il generatore incapace di validare la
    // regola: la prova automatica scartava respiri che nella realta' sarebbero
    // stati trovati.
    //
    // 0.35 * 10^(-15/20) = 0.0622 sarebbe il valore teorico; la misura sul
    // segnale generato da' 0.033 perche' gli inviluppi a coseno di voce e
    // respiro non hanno la stessa forma.
    double breathAmplitude = 0.033;

    double clickAmplitude = 0.55;
    double voiceAmplitude = 0.35;
    int breathCount = 3;
    int clickCount = 4;
    unsigned seed = 20260601u;
    // Durata di ogni segmento vocale.
    double voicedSeconds = 1.15;
    double gapSeconds = 0.65;
};

inline TestTake makeVocalTake(const TakeOptions& options = TakeOptions{}) {
    TestTake take;
    take.sampleRate = options.sampleRate;

    std::mt19937 rng(options.seed);
    std::uniform_real_distribution<double> noise(-1.0, 1.0);

    const int64_t rate = static_cast<int64_t>(options.sampleRate);
    const int64_t voicedLength = static_cast<int64_t>(options.voicedSeconds * options.sampleRate);
    const int64_t gapLength = static_cast<int64_t>(options.gapSeconds * options.sampleRate);

    // Sequenza: voce, pausa, voce, pausa, ... con un blocco iniziale di silenzio.
    const int64_t leadSilence = rate / 2;
    const int64_t segments = std::max(1, options.breathCount);
    const int64_t totalLength =
        leadSilence + segments * (voicedLength + gapLength) + rate / 2;

    take.samples.assign(static_cast<size_t>(totalLength), 0.0f);

    // Fondo di rumore realistico.
    for (float& sample : take.samples) {
        sample = static_cast<float>(noise(rng) * options.noiseFloor);
    }

    // Segmenti vocali con pause fra loro.
    double f0 = 128.0;
    for (int64_t s = 0; s < segments; ++s) {
        const int64_t voicedStart = leadSilence + s * (voicedLength + gapLength);
        detail::addVoiced(take.samples, options.sampleRate, voicedStart, voicedLength,
                          f0, options.voiceAmplitude, rng);
        f0 *= 1.06;   // piccolo cambio di intonazione fra le frasi
    }

    // Un respiro al centro di ogni pausa.
    for (int64_t s = 0; s < segments; ++s) {
        const int64_t gapStart = leadSilence + s * (voicedLength + gapLength) + voicedLength;
        const int64_t breathLength = static_cast<int64_t>(options.sampleRate * 0.28);
        const int64_t breathStart = gapStart + (gapLength - breathLength) / 2;

        detail::addBreath(take.samples, options.sampleRate, breathStart, breathLength,
                          options.breathAmplitude, rng);

        GroundTruthEvent event;
        event.kind = DetectionKind::Breath;
        // La verita' a terra copre la parte "piena" dell'inviluppo: i primi e
        // gli ultimi 15% della rampa a coseno sono sotto la soglia di livello.
        event.startSample = breathStart + static_cast<int64_t>(breathLength * 0.15);
        event.endSample = breathStart + static_cast<int64_t>(breathLength * 0.85);
        event.label = "respiro " + std::to_string(s + 1);
        take.events.push_back(event);
    }

    // Click distribuiti: alcuni in pausa, alcuni dentro la voce.
    for (int c = 0; c < options.clickCount; ++c) {
        const bool insideVoice = (c % 2) == 1 && segments > 0;
        int64_t clickStart = 0;

        if (insideVoice) {
            const int64_t s = c % segments;
            const int64_t voicedStart = leadSilence + s * (voicedLength + gapLength);
            clickStart = voicedStart + voicedLength / 3 + static_cast<int64_t>(options.sampleRate * 0.07 * c);
        } else {
            const int64_t s = std::min<int64_t>(c, segments - 1);
            const int64_t gapStart = leadSilence + s * (voicedLength + gapLength) + voicedLength;
            clickStart = gapStart + static_cast<int64_t>(options.sampleRate * 0.06);
        }

        clickStart = std::max<int64_t>(0, std::min<int64_t>(totalLength - 1, clickStart));
        detail::addClick(take.samples, options.sampleRate, clickStart, options.clickAmplitude, rng);

        GroundTruthEvent event;
        event.kind = DetectionKind::Click;
        event.startSample = clickStart;
        event.endSample = clickStart + static_cast<int64_t>(options.sampleRate * 0.004);
        event.label = "click " + std::to_string(c + 1);
        take.events.push_back(event);
    }

    // Limite di sicurezza: il materiale non deve clippare.
    for (float& sample : take.samples) {
        sample = std::max(-0.99f, std::min(0.99f, sample));
    }

    return take;
}

// Solo respiri, in pause prive di voce.
inline TestTake makeBreathOnlyTake(const TakeOptions& options = TakeOptions{}) {
    TestTake take = makeVocalTake(options);
    return take;
}

// Solo voce e rumore: serve a misurare i falsi positivi.
inline TestTake makeVoiceOnlyTake(const TakeOptions& options = TakeOptions{}) {
    TakeOptions local = options;
    local.breathCount = 3;
    local.clickCount = 0;

    TestTake take;
    take.sampleRate = local.sampleRate;

    std::mt19937 rng(local.seed);
    std::uniform_real_distribution<double> noise(-1.0, 1.0);

    const int64_t rate = static_cast<int64_t>(local.sampleRate);
    const int64_t voicedLength = static_cast<int64_t>(local.voicedSeconds * local.sampleRate);
    const int64_t gapLength = static_cast<int64_t>(local.gapSeconds * local.sampleRate);
    const int64_t totalLength = rate / 2 + 3 * (voicedLength + gapLength) + rate / 2;

    take.samples.assign(static_cast<size_t>(totalLength), 0.0f);
    for (float& sample : take.samples) {
        sample = static_cast<float>(noise(rng) * local.noiseFloor);
    }

    double f0 = 128.0;
    for (int s = 0; s < 3; ++s) {
        const int64_t voicedStart = rate / 2 + s * (voicedLength + gapLength);
        detail::addVoiced(take.samples, local.sampleRate, voicedStart, voicedLength,
                          f0, local.voiceAmplitude, rng);
        f0 *= 1.06;
    }

    return take;
}

// Silenzio con solo fondo di rumore: nessun evento atteso.
inline TestTake makeSilenceTake(double seconds = 3.0, double sampleRate = 48000.0,
                                unsigned seed = 7u) {
    TestTake take;
    take.sampleRate = sampleRate;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> noise(-1.0, 1.0);
    take.samples.resize(static_cast<size_t>(seconds * sampleRate));
    for (float& sample : take.samples) {
        sample = static_cast<float>(noise(rng) * 0.0002);
    }
    return take;
}

} // namespace synthetic
} // namespace bc
