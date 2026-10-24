// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  offline_analyzer.h — Analisi di un intero file
//
//  Sequenza:
//    1. frame features DSP, salienza armonica, click e respiri;
//    2. isteresi, confini, merge e filtri;
//    3. classificatore legacy opzionale da un profilo esplicito.
// L'adapter ONNX appartiene a StudioEngine, separato dal Core.
// ============================================================================
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "core/detect/detection.h"
#include "core/dsp/frame_features.h"

namespace bc {

// Owned by one analysis worker. Exact sample comparison prevents stale reuse
// after edits, even when the source path and duration have not changed.
struct OfflineFeatureCache {
    std::vector<float> samples;
    std::vector<FrameFeatures> frames;
    double sampleRate = 0;
    int fftSize = 0;
    int hopSize = 0;
};

class OfflineAnalyzer {
public:
    using ProgressCallback = std::function<void(float fraction, const char* stage)>;

    OfflineAnalyzer();
    ~OfflineAnalyzer();

    OfflineAnalyzer(const OfflineAnalyzer&) = delete;
    OfflineAnalyzer& operator=(const OfflineAnalyzer&) = delete;

    // Percorso del modello VAD. Se vuoto o non apribile, l'analisi procede
    // senza gate vocale e lo dichiara in AnalysisResult::vadStatus.
    void setProgressCallback(ProgressCallback callback) { progress_ = std::move(callback); }
    void setCancellationCallback(std::function<bool()> callback) { cancelled_ = std::move(callback); }
    void setFeatureCache(OfflineFeatureCache* cache) { cache_ = cache; }

    // Analizza audio mono in [-1, 1]. Non lancia: in caso di problema ritorna
    // un risultato con error compilato.
    AnalysisResult analyze(const float* samples,
                           size_t count,
                           double sampleRate,
                           const DetectorSettings& settings);

private:
    void reportProgress(float fraction, const char* stage);

    ProgressCallback progress_;
    std::function<bool()> cancelled_;
    OfflineFeatureCache* cache_ = nullptr;
};

} // namespace bc
