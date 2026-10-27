// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace bc::studio {

// One request, owned by the caller until analyzeFile returns. Cancellation is
// thread-safe; progress runs on the analysis thread, never the host UI thread.
// A negative fraction means the current phase has no measurable percentage.
struct AnalysisControl {
    std::atomic<bool> cancelRequested{false};
    std::function<void(float, const char*)> progress;

    bool cancelled(std::string* error = nullptr) const {
        if (!cancelRequested.load(std::memory_order_relaxed)) return false;
        if (error) *error = "Analisi annullata";
        return true;
    }
    void report(float fraction, const char* stage) const {
        if (progress) progress(fraction, stage);
    }
};
} // namespace bc::studio
