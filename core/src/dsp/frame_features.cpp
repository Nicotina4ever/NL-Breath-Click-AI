// SPDX-License-Identifier: MPL-2.0
#include "core/dsp/frame_features.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bc {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinMagnitude = 1.0e-12f;
constexpr float kSilenceDb = -160.0f;

float clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

} // namespace

FrameAnalyzer::FrameAnalyzer(const Config& config)
    : config_(config),
      fftCfg_(nullptr, ::free) {
    if (config_.fftSize < 128 || (config_.fftSize % 2) != 0) {
        throw std::invalid_argument("FrameAnalyzer: fftSize deve essere pari e >= 128");
    }
    if (config_.sampleRate <= 0.0f) {
        throw std::invalid_argument("FrameAnalyzer: sampleRate non valido");
    }
    config_.decimation = std::max(1, config_.decimation);

    numBins_ = config_.fftSize / 2 + 1;

    kiss_fftr_cfg cfg = kiss_fftr_alloc(config_.fftSize, 0, nullptr, nullptr);
    if (!cfg) {
        throw std::runtime_error("FrameAnalyzer: allocazione FFT fallita");
    }
    fftCfg_.reset(cfg);

    window_.resize(static_cast<size_t>(config_.fftSize));
    fftInput_.resize(static_cast<size_t>(config_.fftSize));
    fftOutput_.resize(static_cast<size_t>(numBins_));
    magnitude_.resize(static_cast<size_t>(numBins_), 0.0f);
    previousMagnitude_.resize(static_cast<size_t>(numBins_), 0.0f);
    centered_.resize(static_cast<size_t>(config_.fftSize), 0.0f);

    decimatedCount_ = config_.fftSize / config_.decimation;
    decimatedRate_ = config_.sampleRate / static_cast<float>(config_.decimation);
    decimated_.resize(static_cast<size_t>(decimatedCount_) + 8u, 0.0f);

    // Bordi di banda in indici di bin: 80, 500, 2000, 10000 Hz.
    {
        const float edgesHz[] = {80.0f, 500.0f, 2000.0f, 10000.0f};
        bandEdges_.clear();
        for (float hz : edgesHz) {
            const float bin = hz * static_cast<float>(config_.fftSize) / config_.sampleRate;
            bandEdges_.push_back(clampf(bin, 1.0f, static_cast<float>(numBins_ - 1)));
        }
    }

    // Lag di ricerca sul segnale decimato.
    lagMin_ = static_cast<int>(decimatedRate_ / config_.pitchMaxHz);
    lagMax_ = static_cast<int>(decimatedRate_ / config_.pitchMinHz);
    lagMin_ = std::max(2, lagMin_);
    lagMax_ = std::min(decimatedCount_ - 8, lagMax_);
    if (lagMax_ <= lagMin_) {
        lagMin_ = 2;
        lagMax_ = std::max(3, decimatedCount_ / 2);
    }

    buildWindow();
    reset();
}

FrameAnalyzer::~FrameAnalyzer() = default;

void FrameAnalyzer::buildWindow() {
    const int n = config_.fftSize;
    // Finestra di Hann periodica, adatta a frame sovrapposti.
    for (int i = 0; i < n; ++i) {
        window_[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / static_cast<float>(n)));
    }
}

void FrameAnalyzer::reset() {
    std::fill(previousMagnitude_.begin(), previousMagnitude_.end(), 0.0f);
    previousRms_ = 0.0f;
    hasPrevious_ = false;
    features_ = FrameFeatures{};
    features_.levelDb = kSilenceDb;
    features_.highBandLevelDb = kSilenceDb;
}

FrameFeatures FrameAnalyzer::analyze(const float* samples, size_t frameSize) {
    features_ = FrameFeatures{};
    features_.levelDb = kSilenceDb;
    features_.highBandLevelDb = kSilenceDb;

    const int n = config_.fftSize;

    if (!samples || frameSize == 0) {
        std::fill(previousMagnitude_.begin(), previousMagnitude_.end(), 0.0f);
        previousRms_ = 0.0f;
        hasPrevious_ = true;
        return features_;
    }

    // Allineamento a destra: si analizza la parte piu' recente dell'audio.
    const size_t copyCount = std::min<size_t>(frameSize, static_cast<size_t>(n));
    const size_t padCount = static_cast<size_t>(n) - copyCount;
    if (padCount > 0) {
        std::fill(fftInput_.begin(),
                  fftInput_.begin() + static_cast<std::ptrdiff_t>(padCount), 0.0f);
    }
    const float* source = samples + (frameSize - copyCount);
    std::copy(source, source + copyCount,
              fftInput_.begin() + static_cast<std::ptrdiff_t>(padCount));

    // ------------------------------------------------------------ dominio tempo
    double sumSquares = 0.0;
    double diffSquares = 0.0;
    float peak = 0.0f;
    int zeroCrossings = 0;

    for (int i = 0; i < n; ++i) {
        const float sample = fftInput_[static_cast<size_t>(i)];
        sumSquares += static_cast<double>(sample) * sample;
        peak = std::max(peak, std::abs(sample));
        if (i > 0) {
            const float previous = fftInput_[static_cast<size_t>(i - 1)];
            if ((sample >= 0.0f) != (previous >= 0.0f)) {
                ++zeroCrossings;
            }
            const double diff = static_cast<double>(sample) - previous;
            diffSquares += diff * diff;
        }
    }

    const float rms = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(n)));
    features_.rms = rms;
    features_.peak = peak;
    features_.crest = rms > 1.0e-9f ? peak / rms : 0.0f;
    features_.zcr = static_cast<float>(zeroCrossings) / static_cast<float>(n);
    features_.highFreqRatio =
        static_cast<float>(diffSquares / (diffSquares + sumSquares + 1.0e-12));
    features_.levelDb =
        rms > 1.0e-8f ? std::max(kSilenceDb, 20.0f * std::log10(rms)) : kSilenceDb;

    if (hasPrevious_) {
        if (previousRms_ > 1.0e-8f && rms > 1.0e-8f) {
            features_.envelopeSlopeDb = 20.0f * std::log10(rms / previousRms_);
        } else {
            features_.envelopeSlopeDb = rms > previousRms_ ? 60.0f : -60.0f;
        }
    }

    // ----------------------------------------------------------- dominio spettro
    for (int i = 0; i < n; ++i) {
        centered_[static_cast<size_t>(i)] =
            fftInput_[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)];
    }

    kiss_fftr(fftCfg_.get(), centered_.data(), fftOutput_.data());

    double magnitudeSum = 0.0;
    double logSum = 0.0;
    float maxMagnitude = 0.0f;

    for (int k = 0; k < numBins_; ++k) {
        const float re = fftOutput_[static_cast<size_t>(k)].r;
        const float im = fftOutput_[static_cast<size_t>(k)].i;
        const float mag = std::sqrt(re * re + im * im);
        magnitude_[static_cast<size_t>(k)] = mag;
        magnitudeSum += mag;
        maxMagnitude = std::max(maxMagnitude, mag);
        if (mag > kMinMagnitude) {
            logSum += std::log(static_cast<double>(mag));
        }
    }

    const float meanMagnitude = static_cast<float>(magnitudeSum / static_cast<double>(numBins_));
    features_.spectralCrest =
        meanMagnitude > kMinMagnitude ? maxMagnitude / meanMagnitude : 0.0f;

    // Appiattimento spettrale = media geometrica / media aritmetica.
    // Vale ~1 per rumore bianco (respiro, click) e ~0 per suoni tonali (voce).
    if (meanMagnitude > kMinMagnitude) {
        const float geometricMean =
            static_cast<float>(std::exp(logSum / static_cast<double>(numBins_)));
        features_.spectralFlatness = clampf(geometricMean / meanMagnitude, 0.0f, 1.0f);
    }

    const float binHz = config_.sampleRate / static_cast<float>(config_.fftSize);

    if (magnitudeSum > 1.0e-12) {
        double weightedBin = 0.0;
        double weightedBinSq = 0.0;
        for (int k = 0; k < numBins_; ++k) {
            const double mag = magnitude_[static_cast<size_t>(k)];
            weightedBin += mag * k;
            weightedBinSq += mag * static_cast<double>(k) * k;
        }
        const double centroidBin = weightedBin / magnitudeSum;
        features_.spectralCentroid = static_cast<float>(centroidBin) * binHz;
        const double variance =
            std::max(0.0, weightedBinSq / magnitudeSum - centroidBin * centroidBin);
        features_.spectralSpread = static_cast<float>(std::sqrt(variance)) * binHz;

        const double rolloffTarget = magnitudeSum * 0.85;
        double accumulated = 0.0;
        int rolloffBin = numBins_ - 1;
        for (int k = 0; k < numBins_; ++k) {
            accumulated += magnitude_[static_cast<size_t>(k)];
            if (accumulated >= rolloffTarget) {
                rolloffBin = k;
                break;
            }
        }
        features_.spectralRolloff = static_cast<float>(rolloffBin) * binHz;
    }

    // Energie di banda: rapporti + energia assoluta della banda alta.
    {
        const int lowStart = static_cast<int>(bandEdges_[0]);
        const int lowEnd = static_cast<int>(bandEdges_[1]);
        const int midEnd = static_cast<int>(bandEdges_[2]);
        const int highEnd = static_cast<int>(bandEdges_[3]);

        double lowEnergy = 0.0;
        double midEnergy = 0.0;
        double highEnergy = 0.0;
        double highSquares = 0.0;

        for (int k = std::max(0, lowStart); k < lowEnd && k < numBins_; ++k) {
            lowEnergy += magnitude_[static_cast<size_t>(k)];
        }
        for (int k = std::max(0, lowEnd); k < midEnd && k < numBins_; ++k) {
            midEnergy += magnitude_[static_cast<size_t>(k)];
        }
        for (int k = std::max(0, midEnd); k < highEnd && k < numBins_; ++k) {
            const double mag = magnitude_[static_cast<size_t>(k)];
            highEnergy += mag;
            highSquares += mag * mag;
        }

        const double total = lowEnergy + midEnergy + highEnergy;
        if (total > 1.0e-12) {
            features_.lowBandRatio = static_cast<float>(lowEnergy / total);
            features_.midBandRatio = static_cast<float>(midEnergy / total);
            features_.highBandRatio = static_cast<float>(highEnergy / total);
        }

        const int highBins = std::max(1, std::min(highEnd, numBins_) - std::max(0, midEnd));
        features_.highBandEnergy = static_cast<float>(highSquares / highBins);
        features_.highBandLevelDb = features_.highBandEnergy > 1.0e-16f
            ? std::max(kSilenceDb, 10.0f * std::log10(features_.highBandEnergy))
            : kSilenceDb;
    }

    // Flusso spettrale: differenza positiva normalizzata sull'energia corrente.
    if (hasPrevious_ && magnitudeSum > 1.0e-12) {
        double flux = 0.0;
        for (int k = 0; k < numBins_; ++k) {
            const float delta =
                magnitude_[static_cast<size_t>(k)] - previousMagnitude_[static_cast<size_t>(k)];
            if (delta > 0.0f) {
                flux += delta;
            }
        }
        features_.spectralFlux = clampf(static_cast<float>(flux / magnitudeSum), 0.0f, 1.0f);
    }

    features_.harmonicRatio = estimateHarmonicity();

    std::copy(magnitude_.begin(), magnitude_.end(), previousMagnitude_.begin());
    previousRms_ = rms;
    hasPrevious_ = true;

    return features_;
}

float FrameAnalyzer::estimateHarmonicity() {
    // Autocorrelazione normalizzata su segnale decimato.
    //
    // La decimazione (default 4x) riduce il costo di ~16 volte rendendo la
    // stima utilizzabile anche nel percorso realtime. Un filtro a media mobile
    // a 5 tap prima della decimazione limita l'aliasing.
    if (decimatedCount_ < 32) {
        features_.pitchHz = 0.0f;
        return 0.0f;
    }

    const int d = config_.decimation;
    const int taps = 5;
    for (int j = 0; j < decimatedCount_; ++j) {
        const int center = j * d + d / 2;
        float sum = 0.0f;
        int taken = 0;
        for (int t = -taps / 2; t <= taps / 2; ++t) {
            const int index = center + t;
            if (index >= 0 && index < config_.fftSize) {
                sum += fftInput_[static_cast<size_t>(index)];
                ++taken;
            }
        }
        decimated_[static_cast<size_t>(j)] = taken > 0 ? sum / static_cast<float>(taken) : 0.0f;
    }

    double mean = 0.0;
    for (int j = 0; j < decimatedCount_; ++j) {
        mean += decimated_[static_cast<size_t>(j)];
    }
    mean /= static_cast<double>(decimatedCount_);

    double energy = 0.0;
    for (int j = 0; j < decimatedCount_; ++j) {
        const double value = decimated_[static_cast<size_t>(j)] - mean;
        energy += value * value;
    }
    if (energy <= 1.0e-14) {
        features_.pitchHz = 0.0f;
        return 0.0f;
    }

    // ---------------------------------------------------------------------
    //  Autocorrelazione normalizzata
    //  La normalizzazione deve usare l'energia DEI SOLI campioni che
    //  contribuiscono alla somma per quel lag. Dividendo per l'energia totale
    //  del frame il valore risulterebbe artificiosamente piccolo (di un
    //  fattore usable/decimatedCount), falsando la salienza armonica e quindi
    //  la distinzione fra voce e rumore.
    // ---------------------------------------------------------------------
    const int usable = decimatedCount_ - lagMax_;

    std::vector<double> centered(static_cast<size_t>(decimatedCount_), 0.0);
    for (int j = 0; j < decimatedCount_; ++j) {
        centered[static_cast<size_t>(j)] = decimated_[static_cast<size_t>(j)] - mean;
    }

    auto normalizedCorrelation = [&](int lag) -> double {
        double numerator = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        for (int i = 0; i < usable; ++i) {
            const double a = centered[static_cast<size_t>(i)];
            const double b = centered[static_cast<size_t>(i + lag)];
            numerator += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(energyA * energyB);
        return denominator > 1.0e-15 ? numerator / denominator : 0.0;
    };

    int bestLag = 0;
    double bestCorrelation = 0.0;

    for (int lag = lagMin_; lag <= lagMax_; ++lag) {
        const double normalized = normalizedCorrelation(lag);
        if (normalized > bestCorrelation) {
            bestCorrelation = normalized;
            bestLag = lag;
        }
    }

    // ---------------------------------------------------------------------
    //  Correzione dell'errore di ottava
    //  L'autocorrelazione di un segnale periodico ha massimi quasi uguali a
    //  ogni multiplo del periodo: prendendo il massimo assoluto si finisce su
    //  2x, 3x o 4x il periodo reale, cioe' su una frequenza 2-4 volte piu'
    //  bassa. Si cerca quindi il PRIMO lag il cui valore raggiunge quasi il
    //  massimo: quello e' il periodo fondamentale.
    // ---------------------------------------------------------------------
    if (bestLag > lagMin_ && bestCorrelation > 0.0) {
        const double acceptance = bestCorrelation * 0.90;
        for (int lag = lagMin_; lag < bestLag; ++lag) {
            if (normalizedCorrelation(lag) >= acceptance) {
                bestLag = lag;
                break;
            }
        }
    }

    features_.pitchHz = bestLag > 0
        ? decimatedRate_ / static_cast<float>(bestLag)
        : 0.0f;

    // La salienza resta quella del massimo assoluto: e' la misura di quanto il
    // segnale e' periodico, indipendentemente da quale multiplo si sia scelto.
    return clampf(static_cast<float>(bestCorrelation), 0.0f, 1.0f);
}

} // namespace bc
