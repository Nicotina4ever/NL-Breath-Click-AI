// SPDX-License-Identifier: MPL-2.0
#include "core/dsp/click_detector.h"

#include <algorithm>
#include <cmath>

namespace bc {

namespace {

float clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

float smoothstep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) return value >= edge1 ? 1.0f : 0.0f;
    const float x = clampf((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

} // namespace

ClickDetector::ClickDetector(const ClickDetectorConfig& config)
    : config_(config) {
    config_.subWindowSamples = std::max(8, config_.subWindowSamples);
    config_.historyFrames = std::max(16, config_.historyFrames);

    history_.assign(static_cast<size_t>(config_.historyFrames), 0.0f);
    sortScratch_.assign(static_cast<size_t>(config_.historyFrames), 0.0f);
}

ClickDetector::~ClickDetector() = default;

void ClickDetector::setSensitivity(float sensitivity) {
    config_.sensitivity = clampf(sensitivity, 0.0f, 1.0f);
}

void ClickDetector::reset() {
    std::fill(history_.begin(), history_.end(), 0.0f);
    historyCount_ = 0;
    historyWrite_ = 0;
    delayA_ = 0.0;
    delayB_ = 0.0;
    delayCount_ = 0;
    medianAbsError_ = 0.0f;
}

void ClickDetector::pushHistory(float value) {
    history_[historyWrite_] = value;
    historyWrite_ = (historyWrite_ + 1) % history_.size();
    if (historyCount_ < history_.size()) {
        ++historyCount_;
    }
}

float ClickDetector::robustMedian() {
    if (historyCount_ == 0) {
        return 0.0f;
    }

    std::copy_n(history_.begin(), historyCount_, sortScratch_.begin());
    std::sort(sortScratch_.begin(),
              sortScratch_.begin() + static_cast<std::ptrdiff_t>(historyCount_));

    const size_t mid = historyCount_ / 2;
    return (historyCount_ % 2 == 0)
        ? 0.5f * (sortScratch_[mid - 1] + sortScratch_[mid])
        : sortScratch_[mid];
}

ClickScore ClickDetector::processBlock(const float* samples, size_t count) {
    ClickScore result;

    if (!samples || count == 0) {
        return result;
    }

    // ------------------------------------------- errore di predizione d[n]
    float peak = 0.0f;
    int peakIndex = -1;
    int aboveHalf = 0;
    size_t valuesConsidered = 0;

    for (size_t i = 0; i < count; ++i) {
        const double x = samples[i];

        if (delayCount_ >= 2) {
            // d corrisponde al campione delayB_ = x[n-1].
            const double derivative = delayB_ - 0.5 * (delayA_ + x);
            const float magnitude = static_cast<float>(std::abs(derivative));
            ++valuesConsidered;
            if (magnitude > peak) {
                peak = magnitude;
                peakIndex = static_cast<int>(i) - 1;
            }
        }

        delayA_ = delayB_;
        delayB_ = x;
        if (delayCount_ < 2) ++delayCount_;
    }

    if (valuesConsidered == 0) {
        result.peakIndex = -1;
        return result;
    }

    // Secondo passaggio: quanti campioni superano meta' del picco? Serve a
    // distinguere un click (strettissimo) da un attacco di nota (largo).
    if (peak > 0.0f) {
        const double halfPeak = 0.5 * static_cast<double>(peak);
        // Si ricalcola sui campioni gia' consumati usando lo stesso stato: per
        // semplicita' e costo contenuto si analizza il blocco con un secondo
        // predittore locale che riparte dai campioni del blocco.
        for (size_t i = 1; i + 1 < count; ++i) {
            const double derivative =
                static_cast<double>(samples[i]) -
                0.5 * (static_cast<double>(samples[i - 1]) + static_cast<double>(samples[i + 1]));
            if (std::abs(derivative) > halfPeak) {
                ++aboveHalf;
            }
        }
    }

    const float narrowness = valuesConsidered > 0
        ? 1.0f - static_cast<float>(aboveHalf) / static_cast<float>(valuesConsidered)
        : 0.0f;
    result.narrowness = clampf(narrowness, 0.0f, 1.0f);
    result.peak = peak;
    result.peakIndex = peakIndex;

    // ------------------------------------------------------- stima robusta
    const float median = robustMedian();
    result.median = median;

    // Il denominatore non deve mai essere nullo: si usa un pavimento legato
    // alla risoluzione del segnale.
    const float denominator = std::max(median, 1.0e-7f);
    const float ratio = peak / denominator;
    result.ratio = ratio;

    // ---------------------------------------------------------- punteggio
    // Sensibilita': 0.5 -> soglie nominali; alta -> soglie piu' basse.
    // L'escursione e' volutamente contenuta: le soglie nominali stanno in una
    // zona di separazione ampia e non vanno spostate fino a confondere un
    // respiro con un click.
    const float scale = 1.2f - 0.5f * config_.sensitivity;   // 1.2 .. 0.7
    const float low = config_.ratioLow * scale;
    const float high = config_.ratioHigh * scale;

    const float ratioTerm = smoothstep(low, high, ratio);

    // Un evento largo (attacco di nota, plosiva) non e' un click.
    const float narrowTerm = 1.0f - smoothstep(config_.narrowFraction,
                                                config_.narrowFraction * 2.5f,
                                                1.0f - result.narrowness);

    const float levelTerm = smoothstep(config_.minPeakLevel, config_.minPeakLevel * 20.0f, peak);

    float score = ratioTerm * narrowTerm * levelTerm;

    // Pavimento di storia: senza abbastanza campioni la mediana non e'
    // affidabile e si preferisce non rilevare.
    if (historyCount_ < 8) {
        score *= 0.3f;
    }

    result.score = clampf(score, 0.0f, 1.0f);
    result.detected = result.score > 0.0f;

    // La storia registra il picco della sotto-finestra, non il punteggio.
    pushHistory(peak);
    medianAbsError_ = robustMedian();

    return result;
}

} // namespace bc
