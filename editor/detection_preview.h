// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  detection_preview.h — anteprima locale e riconciliazione delle proposte.
//
//  L'anteprima analizza SOLO una finestra di 10-20 secondi scelta intorno alla
//  vista corrente. Non tocca il progetto e non sostituisce le regioni: produce
//  un AnalysisResult separato per il confronto prima/dopo.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/detect/detection.h"
#include "core/edit/audio_document.h"

namespace bc {

struct DetectionPreviewWindow {
    int64_t startFrame = 0;
    int64_t endFrame = 0;
    int sampleRate = 48000;
};

struct DetectionPreviewDelta {
    std::vector<Detection> before;
    std::vector<Detection> after;
    int kept = 0;
    int added = 0;
    int removed = 0;
    // Corrispondenza uno-a-uno: servono al disegno per distinguere gli
    // eventi mantenuti da quelli aggiunti/rimossi.
    std::vector<char> beforeKept;
    std::vector<char> afterKept;

    int beforeCount() const { return static_cast<int>(before.size()); }
    int afterCount() const { return static_cast<int>(after.size()); }
};

// Sceglie la finestra intorno al punto indicato: al massimo `seconds` secondi,
// senza uscire dal file. Se la vista e' piu' stretta, usa la vista.
DetectionPreviewWindow choosePreviewWindow(const AudioDocument& document,
                                            int64_t focusFrame,
                                            int64_t visibleFrames,
                                            double maxSeconds = 15.0);

// Analizza solo i campioni della finestra (mono) con le impostazioni indicate.
AnalysisResult analyzeDetectionPreview(const AudioDocument& document,
                                       const DetectionPreviewWindow& window,
                                       const DetectorSettings& settings);

// Estrae le proposte correnti (solo eventi automatici) come Detection.
std::vector<Detection> detectionsFromDocument(const AudioDocument& document);

DetectionPreviewDelta compareDetectionProposals(
    const std::vector<Detection>& before,
    const std::vector<Detection>& after,
    int64_t overlapToleranceSamples = 1);

// Disegna waveform locale e confronto: prima in alto, dopo in basso.
// `before` e `after` devono essere in coordinate assolute.
void drawDetectionPreview(HDC dc, const RECT& client,
                          const AudioDocument& document,
                          const DetectionPreviewWindow& window,
                          const std::vector<Detection>& before,
                          const std::vector<Detection>& after,
                          const std::wstring& title,
                          const std::wstring& counts);

} // namespace bc
