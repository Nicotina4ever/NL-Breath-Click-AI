// SPDX-License-Identifier: MPL-2.0
#include "core/dsp/resampler.h"

#include <algorithm>
#include <cmath>

namespace bc {

namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

bool LinearResampler::configure(double inputRate, double outputRate) {
    if (inputRate <= 0.0 || outputRate <= 0.0) {
        configured_ = false;
        return false;
    }

    inputRate_ = inputRate;
    outputRate_ = outputRate;
    ratio_ = outputRate / inputRate;

    // Il filtro serve solo in decimazione: in interpolazione non c'e' aliasing
    // da rimuovere e il filtro peggiorerebbe la banda utile.
    filtering_ = ratio_ < 0.999;

    if (filtering_) {
        // Taglio a 0.45 * Nyquist di uscita, con margine per la transizione.
        const double nyquistOut = outputRate * 0.5;
        designLowpass(std::min(nyquistOut * 0.90, inputRate * 0.5 * 0.95));
    } else {
        filters_.clear();
    }

    filtered_.assign(4096, 0.0f);
    reset();
    configured_ = true;
    return true;
}

void LinearResampler::designLowpass(double cutoffHz) {
    // Due biquad di Butterworth in cascata (4o ordine): pendenza adeguata per
    // un gate vocale e costo trascurabile.
    filters_.assign(2, Biquad{});

    const double fs = inputRate_;
    const double f0 = std::max(10.0, std::min(cutoffHz, fs * 0.49));
    const double w0 = 2.0 * kPi * f0 / fs;
    const double cosW0 = std::cos(w0);
    const double sinW0 = std::sin(w0);

    const double qValues[2] = {0.54119610, 1.30656296};   // Butterworth 4o ordine

    for (int stage = 0; stage < 2; ++stage) {
        const double q = qValues[stage];
        const double alpha = sinW0 / (2.0 * q);

        const double b0 = (1.0 - cosW0) * 0.5;
        const double b1 = 1.0 - cosW0;
        const double b2 = (1.0 - cosW0) * 0.5;
        const double a0 = 1.0 + alpha;
        const double a1 = -2.0 * cosW0;
        const double a2 = 1.0 - alpha;

        Biquad& filter = filters_[static_cast<size_t>(stage)];
        filter.b0 = b0 / a0;
        filter.b1 = b1 / a0;
        filter.b2 = b2 / a0;
        filter.a1 = a1 / a0;
        filter.a2 = a2 / a0;
    }
}

void LinearResampler::reset() {
    for (Biquad& filter : filters_) {
        filter.reset();
    }
    phase_ = 0.0;
    previousInput_ = 0.0f;
    hasPrevious_ = false;
}

size_t LinearResampler::outputCountFor(size_t count) const {
    if (!configured_) return count;
    return static_cast<size_t>(static_cast<double>(count) * ratio_) + 2;
}

void LinearResampler::process(const float* input, size_t count, std::vector<float>& output) {
    if (!configured_ || !input || count == 0) {
        return;
    }

    if (filtered_.size() < count) {
        filtered_.resize(count);
    }

    if (filtering_) {
        for (size_t i = 0; i < count; ++i) {
            double value = input[i];
            for (Biquad& filter : filters_) {
                value = filter.process(value);
            }
            filtered_[i] = static_cast<float>(value);
        }
    } else {
        std::copy(input, input + count, filtered_.begin());
    }

    if (!hasPrevious_) {
        previousInput_ = filtered_[0];
        hasPrevious_ = true;
    }

    // Interpolazione lineare con posizione frazionaria persistente fra i blocchi.
    const double step = inputRate_ / outputRate_;
    const size_t reserveCount = outputCountFor(count);
    output.reserve(output.size() + reserveCount);

    double position = phase_;
    while (position < static_cast<double>(count)) {
        const size_t index = static_cast<size_t>(position);
        const double fraction = position - static_cast<double>(index);

        const float a = index == 0 ? previousInput_ : filtered_[index - 1];
        const float b = filtered_[index];
        output.push_back(static_cast<float>(a + (b - a) * fraction));

        position += step;
    }

    // La posizione residua diventa la fase del blocco successivo, riferita al
    // campione corrente come nuovo "precedente".
    phase_ = position - static_cast<double>(count);
    previousInput_ = filtered_[count - 1];
}

} // namespace bc
